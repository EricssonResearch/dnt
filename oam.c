// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.

#include "oam.h"
#include "action.h"
#include "pipeline.h"
#include "conf_oam.h"
#include "hashmap.h"
#include "if_oam.h"
#include "if_oam_cmd.h"
#include "interface.h"
#include "packet.h"
#include "utils.h"
#include "json.h"
#include "log.h"
#include "time_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <netdb.h>

#define OAM_RCVY_RESET_MS 5000
#define OAM_PING_TTL 64
#define OAM_CHANNEL 1 /* Management Communication Channel (MCC), similar format to ours */

struct MepStart {
    char *name;
    char *stream_name;
    struct Pipeline *pipe;
    int pipe_pos_idx;
    int level;
};

struct oam_request{                 // needed for the ping thread. Shuld be used in other function calls too
    FILE *cmd_w;
    char iface_name[32];
    struct Interface *iface;
    const char *return_ip;
    unsigned return_port;
    unsigned session_id, seq;
    const char *type;
    char mep_start[32];              // local stream:mep from where rping starts
    char remote_mep[32], remote_strm[32];       // remote stream:mep from where ping starts
    char mep_stop[32];               // dest mep/mip
    int level, rr, os, dly;
    enum OamReqestMode mode;
    unsigned count;
    unsigned interval_ms;
    unsigned char ttl;
  };

static int nr_oam_ifaces = 0;
static struct Interface *oam_ifaces[16];
static struct Interface *oam_default_iface = NULL;
static struct Interface *oam_cmd_iface = NULL;
static struct HashMap *mep_starts = NULL; // name -> struct MEPStart

struct SessionTracker {
    FILE* cmd_w; //TODO this is not good if the cmd connection closes before we get the reply
    time_t alloc_time;
    pthread_t multireq_tid;
    struct oam_request *req;
    bool live;
};

char *last_stream=NULL;                       // the last stream
struct StreamSessions {
    struct SessionTracker sessions[16];
    unsigned last_session;
};

static struct HashMap *session_ids = NULL; // stream_name -> struct StreamSessions
static pthread_mutex_t session_lock;

bool set_oam_cmd_if(struct Interface *iface)
{
    if (oam_cmd_iface == NULL) {
        oam_cmd_iface = iface;
        return true;
    } else {
        fprintf(stderr, "only one OAM command interface is supported, config has '%s' and '%s'\n",
                oam_cmd_iface->name, iface->name);
        return false;
    }
}

void add_oam_if(struct Interface *iface)
{
    if (nr_oam_ifaces < 16) {
        oam_ifaces[nr_oam_ifaces] = iface;
        if (oam_default_iface == NULL) {
            oam_default_iface = iface;
        } else {
            if (strcmp(iface->name, oam_default_iface->name) < 0)
                oam_default_iface = iface;
        }
        nr_oam_ifaces++;
    }
}

static struct Interface *get_oam_if(const char *name)
{
    for (int i = 0; i < nr_oam_ifaces; ++i) {
        if (strcmp(name, oam_ifaces[i]->name) == 0) {
            return oam_ifaces[i];
        }
    }
    return NULL;
}

static int alloc_session_id(const char *stream_name, struct oam_request *req, FILE *cmd_w)
{
    pthread_mutex_lock(&session_lock);
    struct StreamSessions *stream = hashmap_find(session_ids, stream_name);
    if (stream == NULL) {
        stream = calloc_struct(StreamSessions);
        hashmap_insert(session_ids, strdup(stream_name), stream);
    }

    unsigned next_id = (stream->last_session + 1) % 16;
    unsigned id = next_id;
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

    if (stream->sessions[id].live) {
        if (now.tv_sec > stream->sessions[id].alloc_time + 2) {
            //fprintf(stderr, "session %u timeouted\n", id);
            stream->sessions[id].live = false;
        } else {
            id = (id + 1) % 16;
            while (stream->sessions[id].live && id != next_id) {
                if (now.tv_sec > stream->sessions[id].alloc_time + 2) {
                    //fprintf(stderr, "session %u timeouted\n", id);
                    stream->sessions[id].live = false;
                    break;
                }
                id = (id + 1) % 16;
            }
        }
    }

    if (stream->sessions[id].live) {
        pthread_mutex_unlock(&session_lock);
        return -1;
    } else {
        stream->last_session = id;
        stream->sessions[id].live = true;
        stream->sessions[id].alloc_time = now.tv_sec + 1;
        stream->sessions[id].req = req;
        stream->sessions[id].cmd_w = cmd_w;
        pthread_mutex_unlock(&session_lock);
        return id;
    }
}

int oam_create_mep_start(const char *stream_name, const char *mep_name, int level, unsigned idx)
{
    if (mep_starts == NULL) {
        mep_starts = new_hashmap(59, NULL, NULL);
    }
    struct MepStart *mepstart = hashmap_find(mep_starts, mep_name);
    if (mepstart) {
        fprintf(stderr, "MEP Start '%s' defined twice, in streams '%s' and '%s'\n",
                mep_name, mepstart->stream_name, stream_name);
        return -1;
    }
    mepstart = calloc_struct(MepStart);
    char *fullname = malloc(strlen(stream_name) + strlen(mep_name) + 2);
    strcpy(fullname, stream_name);
    strcat(fullname, ":");
    strcat(fullname, mep_name);
    mepstart->name = fullname;
    mepstart->stream_name = strdup(stream_name);
    mepstart->level = level;
    mepstart->pipe_pos_idx = idx;
    // for mepstart->pipe see oam_set_pipeline_for_mep_start()
    hashmap_insert(mep_starts, mepstart->name, mepstart);
    return 0;
}

struct SetPipeParam {
    const char *stream_name;
    struct Pipeline *pipe;
};

static int set_pipe_cb(const char *key, void *value, void *userdata)
{
    (void)key;
    struct MepStart *mepstart = value;
    struct SetPipeParam *params = userdata;

    if (strcmp(mepstart->stream_name, params->stream_name) == 0)
        mepstart->pipe = params->pipe;

    return 1;
}

void oam_set_pipeline_for_mep_start(const char *stream_name, struct Pipeline *pipe)
{
    struct SetPipeParam params = {stream_name, pipe};
    hashmap_foreach(mep_starts, set_pipe_cb, &params);
}

static int add_fixed_headers(struct Packet *packet, unsigned char ttl,
        unsigned char seq, unsigned short channel, unsigned short nodeid,
        unsigned char level, unsigned char session)
{
    unsigned int proto_id = PROTO_ID_MPLS;
    packet_add_header(packet, 0, proto_id, protocol_list[proto_id].bytelength);
    proto_id = PROTO_ID_OAM;
    packet_add_header(packet, 1, proto_id, protocol_list[proto_id].bytelength);

    unsigned char *mpls = packet->buf + packet->headers[0].start;
    mpls[0] = 0;
    mpls[1] = 0;
    mpls[2] = 1; // BOS
    mpls[3] = ttl;
    unsigned char *oam  = packet->buf + packet->headers[1].start;
    oam[0] = 0x11; // indicator and version
    oam[1] = seq;
    oam[2] = (channel>>8) & 0xff;
    oam[3] = channel & 0xff;
    oam[4] = (nodeid>>8) & 0xff;
    oam[5] = nodeid & 0xff;
    oam[6] = (level & 0x07) << 1;
    oam[7] = session & 0x0f;

    return 0;
}

static int oam_send_request(struct oam_request *req){
    struct Packet *packet = new_packet(NULL);
    unsigned short node_id = oamif_get_uid(req->iface);

    add_fixed_headers(packet, req->ttl, req->seq, OAM_CHANNEL,
            node_id, req->level, req->session_id);
    packet->ttl = req->ttl;

    // set the packet receive time the same (needed if further MIP/MEP is on pipeline)
    struct timespec sendtime;
    clock_gettime(CLOCK_REALTIME, &sendtime);
    packet->recv_time = sendtime;

    struct MepStart *mep = hashmap_find(mep_starts, req->mep_start);
    if (!mep) {
        log_error(OAM, "Invalid mep_start: %s", req->mep_start);
        return -EINVAL; //TODO do we really need this error code?
    }

    struct JsonValue *js = json_object();
    json_object_insert(js, "request", json_string(req->type));
    json_object_insert(js, "req_type", json_string("request"));
    json_object_insert(js, "stream", json_string(mep->stream_name));
    json_object_insert(js, "level", json_number(req->level));
    struct JsonValue *jret = json_object();
    json_object_insert(jret, "ip", json_string(req->return_ip));
    json_object_insert(jret, "port", json_number(req->return_port));
    json_object_insert(js, "return", jret);

    if(strcmp(req->type, "ping")==0){
        json_object_insert(js, "target", json_string(req->mep_stop));
        if(req->rr == 1){
            jret = json_array();
            json_array_unshift(jret, json_string(mep->name));
            json_object_insert(js, "rr", jret);
        }
        if(req->os == 1){
            jret = json_true();
            json_object_insert(js, "objects", jret);
        }
        if(req->dly == 1){
            jret = json_true();
            json_object_insert(js, "delay", jret);
        }
        if(req->mode == OAM_CFG)
            json_object_insert(js, "mode", json_string("cfg"));

        json_object_insert(js, "send_s", json_number(sendtime.tv_sec));
        json_object_insert(js, "send_ns", json_number(sendtime.tv_nsec));
    }
    else if(strcmp(req->type, "rping")==0){
        json_object_insert(js, "target", json_string(req->remote_mep));
        char cmd_str[128];  // restriction: for remote ping the default interface must be used (here ping@iface is invalid)
        sprintf(cmd_str, "ping %s:%s %s %d", req->remote_strm,req->remote_mep, req->mep_stop, req->level);
        if(req->rr == 1) strcat(cmd_str, " -r");
        if(req->os == 1) strcat(cmd_str, " -o");
        json_object_insert(js, "command", json_string(cmd_str));
    } else {
        //fprintf(cmd_w, "invalid request '%s'\n", req->type);
        return -EINVAL; //TODO do we really need this error code?
    }

    unsigned js_length;
    char *js_string;
    js_string = json_serialize(js, &js_length);
    json_delete(js);
    if (js_string == NULL) {
        //TODO can this happen?
        delete_packet(packet);
        return -ENOMSG;
    }
    packet_add_header(packet, 2, PROTO_ID_PAYLOAD, js_length);
    unsigned char *msg = packet->buf + packet->headers[2].start;
    memcpy(msg, js_string, js_length);

    log_packet(OAM, "%s:%d seq %d lvl %d S - %s",
                    mep->stream_name, req->session_id, req->seq, req->level,
                    js_string);

    free(js_string);

    struct PipelineIterator *pi = new_pipe_iterator(mep->pipe, packet);
    pi->pos = mep->pipe_pos_idx;

    pipe_iterator_run(pi);
    return 0;
}

static void *oam_ping_thread(void *arg)
{
    struct oam_request *req = (struct oam_request *)arg;
    unsigned seq=0;

    while(1){
        if((req->count != 0) && (seq >= req->count)) break;
        req->seq = seq & 0xFF;
        oam_send_request(req);
        usleep(req->interval_ms * 1000);
        seq++;
    }
    free(req);
    return NULL;
}

static int oam_ping(struct oam_request *ping_req)
{
    FILE *cmd_w = ping_req->cmd_w;

    struct MepStart *mep = hashmap_find(mep_starts, ping_req->mep_start);
    if (!mep) {
        fprintf(cmd_w, "invalid mep start name '%s'\n", ping_req->mep_start);
        return -EINVAL; //TODO do we really need this error code?
    }
    struct Pipeline *pipe = mep->pipe;
    if (!pipe) {
        fprintf(cmd_w, "mep start '%s' has no pipeline!?!\n", ping_req->mep_start);
        return -EINVAL;
    }

    int session_id = alloc_session_id(mep->stream_name, ping_req, cmd_w);
    if (session_id < 0) {
        fprintf(cmd_w, "mep start '%s' has no free session id\n", ping_req->mep_start);
        return -EINVAL;
    }
    if(ping_req->mode == OAM_CLI)               // update only for CLI streams
        last_stream = mep->stream_name;

    if(strlen(ping_req->iface_name) == 0)
        ping_req->iface = oam_default_iface;
    else
        ping_req->iface = get_oam_if(ping_req->iface_name);
    if (ping_req->iface == NULL) {
        fprintf(cmd_w, "invalid interface name: %s", ping_req->iface_name);
        return -EINVAL;
    }

    if(ping_req->return_ip == NULL){    // in case of rping this is not NULL
        ping_req->return_ip = oamif_get_ip(ping_req->iface);
        ping_req->return_port = oamif_get_port(ping_req->iface);
    }

    ping_req->session_id = session_id;
    ping_req->seq = 0;

    log_info(OAM, "OAM packet %s session %u seq %u, %s -> %s, level %d, count %d interval %d, rr: %s os: %s\t[reply to ip: %s, port: %u]",
            ping_req->type, ping_req->session_id, ping_req->seq, ping_req->mep_start, ping_req->mep_stop, ping_req->level, ping_req->count, ping_req->interval_ms,
            ping_req->rr?"yes":"no", ping_req->os?"yes":"no", oamif_get_ip(ping_req->iface), oamif_get_port(ping_req->iface));

    fprintf(cmd_w, "OAM packet %s session %u seq %u, %s -> %s, level %d, count %d interval %d, rr: %s os: %s\t[reply to ip: %s, port: %u]\n",
            ping_req->type, ping_req->session_id, ping_req->seq, ping_req->mep_start, ping_req->mep_stop, ping_req->level, ping_req->count, ping_req->interval_ms,
            ping_req->rr?"yes":"no", ping_req->os?"yes":"no", oamif_get_ip(ping_req->iface), oamif_get_port(ping_req->iface));

    if(ping_req->count == 1){
          return oam_send_request(ping_req);
          free(ping_req);
    } else {
          pthread_attr_t attr;
          if ((errno = pthread_attr_init(&attr)) != 0) {
              perror("oam ping thread pthread_attr_init");
              return -1;
          }

          struct StreamSessions *stream = hashmap_find(session_ids, mep->stream_name);
          if (pthread_create(&stream->sessions[session_id].multireq_tid, &attr, &oam_ping_thread, ping_req) != 0) {
              fprintf(stderr, "could not create new ping thread\n");
              return -1;
          }
    }

    return 0;
}

static struct oam_request* oam_parse_ping(const char *oam_command, int mode, FILE *cmd_w)
{
    char c;
    int k, val, l;
    float fval;

    struct oam_request *ping_req = calloc_struct(oam_request);
    ping_req->cmd_w = cmd_w;
    ping_req->seq = 0;
    ping_req->level = 0;
    ping_req->ttl = OAM_PING_TTL;
    ping_req->rr = 0;
    ping_req->os = 0;
    ping_req->dly = 0;
    ping_req->mode = mode;
    ping_req->interval_ms = 1000;
    // Init return_ip to NULL
    ping_req->return_ip = NULL;
    ping_req->return_port = 0;

    if(mode == OAM_CFG)
        ping_req->count = 0;
    else
        ping_req->count = 1;

    if(strncmp(oam_command, "ping", 4) == 0){
        if(oam_command[4]=='@'){
            k = sscanf(oam_command, "ping@%s %s %s %d%n",
                    ping_req->iface_name, ping_req->mep_start, ping_req->mep_stop, &ping_req->level, &l);
            if (k < 4) {
                fprintf(cmd_w, "ping arguments invalid");
            }
            /*ping_req->iface = get_oam_if(ping_req->iface_name);
            if (ping_req->iface == NULL) {
                fprintf(cmd_w, "invalid interface name: %s", ping_req->iface_name);
            }*/
        } else {
            k = sscanf(oam_command, "ping %s %s %d%n",
                    ping_req->mep_start, ping_req->mep_stop, &ping_req->level, &l);
            if (k < 3) {
                fprintf(cmd_w, "ping arguments invalid");
            }
            ping_req->iface_name[0]=0;
        }
        ping_req->type = "ping";
        ping_req->remote_mep[0]=0;
    }
    else if(strncmp(oam_command, "rping", 5) == 0){
        char remote_strm_mep[32];
        if(oam_command[5]=='@'){
            k = sscanf(oam_command, "rping@%s %s %s %s %d%n",
                    ping_req->iface_name, remote_strm_mep, ping_req->mep_start, ping_req->mep_stop, &ping_req->level, &l);
            if (k < 4) {
                fprintf(cmd_w, "rping arguments invalid");
            }
            /*ping_req->iface = get_oam_if(ping_req->iface_name);
            if (ping_req->iface == NULL) {
                fprintf(cmd_w, "invalid interface name: %s", ping_req->iface_name);
            }*/
        } else {
            k = sscanf(oam_command, "rping %s %s %s %d%n",
                    remote_strm_mep, ping_req->mep_start, ping_req->mep_stop, &ping_req->level, &l);
            if (k < 3) {
                fprintf(cmd_w, "rping arguments invalid");
            }
            ping_req->iface_name[0]=0;
        }
        char *ch=strtok(remote_strm_mep,":");
        if(ch == NULL)
            fprintf(cmd_w, "rping remote stream:mep invalid");
        ch=strtok(NULL,":");
        strcpy(ping_req->remote_mep, ch);
        strcpy(ping_req->remote_strm, remote_strm_mep);
        ping_req->type = "rping";
    }
    else{
        fprintf(cmd_w, "invalid command");
        return NULL;
    }

    // process options
    const char *po = oam_command + l;
    bool opt_err = false;
    while ((k=sscanf(po, " -%c%n", &c, &l)) == 1) {
        if (!isspace(*po)) {
            fprintf(cmd_w, "Error: ping options must be separated by space\n");
            opt_err = true;
            break;
        }
        po += l;
        if (c=='r') {
            ping_req->rr = 1;
        } else if (c=='o') {
            ping_req->os = 1;
        } else if (c=='d') {
            ping_req->dly = 1;
        } else if (c=='i') {
            k = sscanf(po, " %f%n", &fval, &l);
            if (k == 1) {
                po += l;
                if (fval < 0.002) fval = 0.002; // 2msec is the minimum
                ping_req->interval_ms = fval * 1000;
            } else {
                fprintf(cmd_w, "Error: ping interval is invalid\n");
                opt_err = true;
                break;
            }
        } else if (c=='n') {
            if(mode == OAM_CFG){
                fprintf(cmd_w, "Error: ping count should not be used in config.\n");
                opt_err = true;
                break;
            }
            k = sscanf(po, " %d%n", &val, &l);
            if (k == 1) {
                po += l;
                ping_req->count = val;
            } else {
                fprintf(cmd_w, "Error: ping count is invalid\n");
                opt_err = true;
                break;
            }
        } else if (c=='t') {
            k = sscanf(po, " %d%n", &val, &l);
            if (k == 1) {
                po += l;
                ping_req->ttl = val;
            } else {
                fprintf(cmd_w, "Error: ping ttl is invalid\n");
                opt_err = true;
                break;
            }
        } else {
            fprintf(cmd_w, "Error: ping option '%c' is invalid\n", c);
            opt_err = true;
            break;
        }
    }
    if (opt_err) return NULL;
    while(isspace(*po)) po++;
    if (*po) {
        fprintf(cmd_w, "ping options '%s' is invalid", po);
    }
    return ping_req;
}

// any better name for this function??
int oam_start_ping(const char *command, const char *dest_ip, int dest_port, int mode, FILE *cmd_w)
{
    struct oam_request *ping_req = NULL;

    if( (ping_req = oam_parse_ping(command, mode, cmd_w)) == NULL)
        return -EINVAL;

    // for rping, these should be containing the return address
    ping_req->return_ip = dest_ip;
    ping_req->return_port = dest_port;

    return oam_ping(ping_req);

}

static const char help_str[] =
    "Available commands:\n"
    "help - get help\n"
    "exit - exit OAM\n"
    "mode <mode> - terminal mode. Mode can be 'dump' or 'json'.\n"
    "list - list monitoring start points\n"
    "sessions [stream] - list active sessions for 'stream'. List all sessions if no 'stream' specified.\n"
    "stop [stream session_id] - stop a running OAM session, identified by stream:session_id. Stops the last session if no parameters given.\n"
    "returns - list return interfaces\n"
    "ping[@if] <stream:mep-start> <mep-stop/mip/any> <level> [-r] [-o] [-i <interval>] [-n <count>] [-t <ttl>]\n"
    "rping[@if] <remote stream:mep-stop/mip> <stream:mep-start> <mep-stop/mip/any> <level> [-r] [-o] [-i <interval>] [-n <count>] [-t <ttl>]\n";

struct ListParams {
    FILE *cmd_w;
};
static int list_mep_cb(const char *key, void *value, void *userdata)
{
    struct MepStart *start = value;
    struct ListParams *params = userdata;
    fprintf(params->cmd_w, "%s level %d\n", key, start->level);
    return 1;
}
static int list_session_cb(const char *key, void *value, void *userdata)
{
    struct ListParams *params = userdata;
    struct StreamSessions *stream = value;
    for(int i=0; i<16; i++){
        if(stream->sessions[i].live){
            struct oam_request *req = stream->sessions[i].req;
            fprintf(params->cmd_w,"\t%s:%d\t %s %s -> %s level %d\n", key, i, req->type, req->mep_start, req->mep_stop, req->level);
        }
    }
    return 1;
}

static void stop_session(const char *streamname, int session, FILE *cmd_w)
{
    struct StreamSessions *stream = hashmap_find(session_ids, streamname);
    if (stream == NULL) {
        fprintf(cmd_w, "Invalid stream name '%s'.\n", streamname);
    } else {
        if(session==-1)
            session = stream->last_session;
        fprintf(cmd_w, "Stopping stream:session %s:%d ", streamname, session);
        if(stream->sessions[session].live){
            if (pthread_cancel(stream->sessions[session].multireq_tid) <  0)
                fprintf(cmd_w," - failed.\n");
            else{
                stream->sessions[session].live = false;
                fprintf(cmd_w," - stopped.\n");
            }
        } else
            fprintf(cmd_w," - not running.\n");
    }
}

static int close_sessions_cb(const char *key, void *value, void *userdata)
{
    struct ListParams *params = userdata;       // NULL value for cmd_w means close all
    struct StreamSessions *stream = value;
    for(int i=0; i<16; i++){
        if(stream->sessions[i].live && ((params->cmd_w==NULL) || (stream->sessions[i].cmd_w == params->cmd_w)) ){
            stop_session(key, i, stderr);
        }
    }
    return 1;
}

int oam_command_loop(struct Interface *iface)
{
#define ERROR(msg, ...)                             \
    fprintf(cmd_w, "Error: " msg "\n",              \
        ##__VA_ARGS__);                             \
    continue

    int cmd_fd = oam_get_cmd_fd(iface);
    FILE *cmd_w = oam_get_cmd_w(iface);
    int k,l;

    char oam_command[255], last_command[255];
    char streamname[32];

    if (oam_default_iface) {
        fprintf(cmd_w, "OAM ready.\n");
    } else {
        fprintf(cmd_w, "OAM has no configured return interface.\n");
        return -1;
    }

    while (true) {
        //int level=0, rr=0, count=1, interval_ms=1000, os=0, ttl=OAM_PING_TTL;
        int n = read(cmd_fd, oam_command, sizeof(oam_command)-1);
        if (n > 0) {
            oam_command[n] = 0;
            // cut off "\r\n"
            while (n > 0 && iscntrl(oam_command[n-1])) oam_command[--n] = 0;
            //printf("oam command '%s' length %d\n", oam_command, n);

            if (n == 0) continue;

            if (strcmp(oam_command, "\x1b[A") == 0) {
                strcpy(oam_command, last_command);
                fprintf(cmd_w, "%s\n", oam_command);
            } else {
                char *p = oam_command;
                while (isspace(*p)) p++;
                if (*p != 0)
                    strcpy(last_command, oam_command);
                else
                    continue;
            }

            if( (strcmp(oam_command, "exit") == 0) || (strcmp(oam_command, "quit") == 0) ){
                fprintf(cmd_w, "Exiting.\n");
                break;
            }
            else if(strncmp(oam_command, "mode", 4) == 0){
                char *mode_str = oam_command + 4;
                while (isspace(*mode_str)) mode_str++;
                if (*mode_str) {
                    if (strcmp(mode_str, "dump") == 0) {
                        oam_cmd_set_mode(iface, DUMP);
                    } else if (strcmp(mode_str, "json") == 0) {
                        oam_cmd_set_mode(iface, JSON);
                    }else{
                        ERROR("mode argument is invalid");
                    }
                }
                fprintf(cmd_w, "Display mode is %s\n", (oam_cmd_get_mode(iface) == DUMP)? "DUMP":"JSON");
            }
            else if(strcmp(oam_command, "help") == 0){
                fprintf(cmd_w, help_str);
            }
            else if (strcmp(oam_command, "list") == 0) {
                fprintf(cmd_w, "Available MEP Start points:\n");
                struct ListParams params = {cmd_w};
                hashmap_foreach_sorted(mep_starts, list_mep_cb, &params);
            }
            else if (strncmp(oam_command, "sessions", 8) == 0) {
                struct ListParams params = {cmd_w};
                k=sscanf(oam_command, "sessions %s%n", streamname, &l);
                if(k==-1){
                    fprintf(cmd_w, "Sessions:\n");
                    if (!hashmap_foreach(session_ids, list_session_cb, &params))
                        fprintf(stderr, "failed to get session.\n");
                }
                else if(k==1){
                    struct StreamSessions *stream = hashmap_find(session_ids, streamname);
                    fprintf(cmd_w, "Sessions for stream %s:\n", streamname);
                    if (stream == NULL) {
                        fprintf(cmd_w, "Invalid stream name '%s'.\n", streamname);
                    } else {
                        list_session_cb(streamname, stream, &params);
                    }
                }
                else
                    fprintf(cmd_w, "Invalid parameters for 'sessions' command.\n");
            }
            else if (strncmp(oam_command, "stop", 4) == 0) {
                int session;
                k=sscanf(oam_command, "stop %s %d", streamname, &session);
                if(k==-1){
                    if(last_stream == NULL)
                        fprintf(cmd_w,"No previous command to stop.\n");
                    else
                        stop_session(last_stream, -1, cmd_w);
                } else if(k==2){
                    stop_session(streamname, session, cmd_w);
                } else
                    fprintf(cmd_w,"invalid parameters for stop.\n");
            }
            else if (strcmp(oam_command, "returns") == 0) {
                fprintf(cmd_w, "Available OAM return interfaces:\n");
                for (int i=0; i<nr_oam_ifaces; i++) {
                    const char *return_ip = oamif_get_ip(oam_ifaces[i]);
                    unsigned return_port = oamif_get_port(oam_ifaces[i]);
                    fprintf(cmd_w, "%s ip %s port %u",
                            oam_ifaces[i]->name, return_ip, return_port);
                    if (oam_ifaces[i] == oam_default_iface) {
                        unsigned short node_id = oamif_get_uid(oam_ifaces[i]);
                        fprintf(cmd_w, " (default, node id %u)\n", node_id);
                    } else {
                        fprintf(cmd_w, "\n");
                    }
                }
            }
            else if( oam_start_ping(oam_command, NULL, 0, OAM_CLI, cmd_w) != 0){
                ERROR("unknown command '%s'", oam_command);
            }
        }
        else {
            break;
        }
    }
    printf("Telnet closed.\n");
    // cleanup
    struct ListParams params = {cmd_w};
    hashmap_foreach(session_ids, close_sessions_cb, &params);

    return 0;
}

/*
 *  Formatted dump functions for printing object specific json
 */
static int dump_seqgen_state(char *str, struct JsonValue *jos){
    char tmp[128];
    struct JsonValue *ini = json_object_get_bool(jos, "use_init_flag");
    if(ini == NULL) {
        fprintf(stderr, "No use_init_flag in object in reply.\n");
        return -1;
    }
    struct JsonValue *rst = json_object_get_bool(jos, "use_reset_flag");
    if(rst == NULL) {
        fprintf(stderr, "No use_init_flag in object in reply.\n");
        return -1;
    }
    snprintf(tmp, sizeof(tmp), " (use_init_flag: %s, use_reset_flag: %s)\n", (ini->type == JSON_TRUE)? "true":"false", (rst->type == JSON_TRUE)? "true":"false" );
    strcat(str, tmp);

    return 0;
}

static int dump_seqrec_state(char *str, struct JsonValue *jos){
    char tmp[1000] = { 0 };
    bool vector = false;
    struct JsonValue *type = json_object_get_string(jos, "type");
    struct JsonValue *reset_msec = json_object_get_number(jos, "reset_msec");
    struct JsonValue *algo = json_object_get_string(jos, "recovery_algorithm");
    struct JsonValue *seq = json_object_get_number(jos, "recovery_seq_num");
    struct JsonValue *passed = json_object_get_number(jos, "passed_packets");
    struct JsonValue *discarded = json_object_get_number(jos, "discarded_packets");
    struct JsonValue *resets = json_object_get_number(jos, "seq_recovery_resets");

    struct JsonValue *hist_len, *reset_flag, *init_flag, *err_paths, *err_resets, *errs, *hist;
    if (!(type && reset_msec && algo && seq && passed && discarded && resets)) {
        fprintf(stderr, "Malformed recovery object state reply\n");
        return -1;
    }
    if (strcmp(type->v.string, "match") != 0) {
        vector = true;
        hist_len = json_object_get_number(jos, "history_length");
        reset_flag = json_object_get_bool(jos, "use_reset_flag");
        init_flag = json_object_get_bool(jos, "use_init_flag");
        err_paths = json_object_get_number(jos, "latent_error_paths");
        err_resets = json_object_get_number(jos, "latent_error_resets");
        errs = json_object_get_number(jos, "latent_errors");
        hist = json_object_get_string(jos, "history");
        if (!(hist_len && reset_flag && init_flag && err_paths && err_resets && errs && hist)) {
            fprintf(stderr, "Malformed vector recovery object state reply\n");
            return -1;
        }
    }
    const char *fmt_match = "\n\t\trecovery_algorithm: %s, reset_timer: %.0fms\n" \
                            "\t\tlatest_valid_sequence_number: %.0f, passed: %.0f, discarded: %.0f\n" \
                            "\t\tnumber_of_resets: %.0f\n";

    const char *fmt_vector = "\n\t\trecovery_algorithm: %s, use_init_flag: %s, use_reset_flag: %s\n" \
                            "\t\treset_timer: %.0fms, history_length: %.0f\n" \
                            "\t\tlatest_valid_sequence_number: %.0f, passed: %.0f, discarded: %.0f\n" \
                            "\t\thistory_content: %s\n" \
                            "\t\tlatent_error_paths: %.0f, latent_error_resets: %.0f\n"
                            "\t\tnumber_of_resets: %.0f\n";
    if (vector) {
        bool init = init_flag->type == JSON_TRUE ? true : false;
        bool reset = reset_flag->type == JSON_TRUE ? true : false;
        snprintf(tmp, sizeof(tmp), fmt_vector, algo->v.string, init ? "true" : "false", reset ? "true" : "false",
                reset_msec->v.number, hist_len->v.number,
                seq->v.number, passed->v.number, discarded->v.number,
                hist->v.string,
                err_paths->v.number, errs->v.number,
                resets->v.number);
    } else {
        snprintf(tmp, sizeof(tmp), fmt_match, algo->v.string, reset_msec->v.number,
                seq->v.number, passed->v.number, discarded->v.number,
                resets->v.number);
    }
    strcat(str, tmp);
    return 0;
}

static int dump_repl_state(char *str, struct JsonValue *jos){
    char tmp[128];
    struct JsonValue *pass = json_object_get_number(jos, "packets_passed");
    if(pass == NULL) {
        fprintf(stderr, "No packets_passed in object in reply.\n");
        return -1;
    }
    snprintf(tmp, sizeof(tmp), "\n\t\tpackets_passed: %.0f\n",  pass->v.number);
    strcat(str, tmp);

    return 0;
}

static int dump_pof_state(char *str, struct JsonValue *jos){
    char tmp[128] = { 0 };
    struct JsonValue *buff_size = json_object_get_number(jos, "max_buffer_length");
    struct JsonValue *max_delay = json_object_get_number(jos, "max_delay");
    struct JsonValue *take_any_time = json_object_get_number(jos, "take_any_time");
    struct JsonValue *buff_len = json_object_get_number(jos, "current_buffer_length");
    struct JsonValue *last_sent = json_object_get_number(jos, "last_sent");
    if (!(buff_size && max_delay && take_any_time && buff_len && last_sent)) {
        fprintf(stderr, "Malformed POF object state reply");
        return -1;
    }
    const char *fmt = "\n\t\tmax_buffer_length: %.0f, max_delay: %.0fms, take_any_time: %.0fms\n" \
                        "\t\tcurrent_buffer_length: %.0f, last_sent: %.0f\n";
    snprintf(tmp, sizeof(tmp), fmt, buff_size->v.number, max_delay->v.number, take_any_time->v.number,
            buff_len->v.number, last_sent->v.number);
    strcat(str, tmp);
    return 0;
}
/*
 * Handle received UDP OAM reply mesage
 * Msg: pointer to the message
 * Return 0 on success
*/
int oam_recv_reply(const char *msg)
{

    // We need to parse for logging, even if JSON mode is used.
    bool cfg_mode = false;

    char reply_str[1400], rr_str[512], obj_str[1000];
    struct JsonValue *j = json_parse(msg, strlen(msg));
    if (j == NULL) {
        //fprintf(stderr, "JSON in reply is invalid.\n");
        log_error(OAM, "JSON in reply is invalid.");
        return -1;
    }
    struct JsonValue *mode = json_object_get_string(j, "mode");
    if(mode!=NULL) {
        if(strcmp(mode->v.string,"cfg") == 0){
            cfg_mode = true;
        }
    }
    struct JsonValue *nid = json_object_get_number(j, "nodeid");
    if(nid==NULL) {
        log_error(OAM, "No nodeid in reply.");
        return -1;
    }
    struct JsonValue *request = json_object_get_string(j, "request");
    if(request == NULL) {
        log_error(OAM, "No request in reply.");
        return -1;
    }
    struct JsonValue *target = json_object_get_string(j, "target");
    if(target == NULL) {
        log_error(OAM, "No target in reply.");
        return -1;
    }
    struct JsonValue *seq = json_object_get_number(j, "sequence");
    if(seq == NULL) {
        log_error(OAM, "No sequence in reply.");
        return -1;
    }
    struct JsonValue *level = json_object_get_number(j, "level");
    if(level == NULL) {
        log_error(OAM, "No level in reply.");
        return -1;
    }
    struct JsonValue *node = json_object_get_string(j, "node");
    if(node == NULL) {
        log_error(OAM, "No node in reply.");
        return -1;
    }
    struct JsonValue *strm = json_object_get_string(j, "stream");
    if(strm == NULL) {
        log_error(OAM, "No stream in reply.");
        return -1;
    }

    struct JsonValue *sess = json_object_get_number(j, "session");
    struct SessionTracker *session = NULL;
    if(sess == NULL) {
        log_error(OAM, "No session id in reply.");
        return -1;
    } else {
        if (sess->v.number < 0 || sess->v.number > 15) {
            log_error(OAM, "session id %.0f in reply is invalid", sess->v.number);
            return -1;
        } else {
            // ToDo: for rping this will not find the stream. Either don't check, or config the streams to check.
            // For now, we get an error but it will work as the session is not used anywhere.
            // TODO for rping we should have two stream id/session: the originator and the source
            //      here we are either the originator or a third party
            struct StreamSessions *stream = hashmap_find(session_ids, strm->v.string);
            if (stream == NULL) {
                log_error(OAM, "Invalid stream name '%s' in reply.", strm->v.string);
                //return -1;
            } else {
                session = &stream->sessions[(int)(sess->v.number)];
                if (!session->live) {
                    log_error(OAM, "Reply for non-live session %.0f of stream '%s'.", sess->v.number, strm->v.string);
                    //return -1;
                }
            }
        }
    }

    log_packet(OAM, "%s:%.0f seq %.0f lvl %.0f D - %s", strm->v.string, sess->v.number, seq->v.number, level->v.number, msg);

    struct JsonValue *dly = json_object_get_bool(j, "delay");
    if(dly != NULL && dly->type == JSON_TRUE){
        // calculate delay
        struct timespec sendtime, receivetime, delay_diff;
        sendtime.tv_sec = json_object_get_number(j, "send_s")->v.number;
        sendtime.tv_nsec = json_object_get_number(j, "send_ns")->v.number;
        receivetime.tv_sec = json_object_get_number(j, "recv_s")->v.number;
        receivetime.tv_nsec = json_object_get_number(j, "recv_ns")->v.number;
        timespecsub(&receivetime, &sendtime, &delay_diff);

        sprintf(reply_str,"[nodeid %.0f session %.0f seq %.0f]\t%s level %.0f on stream %s target %s\treply from %s delay %ld.%ld",
                nid->v.number, sess->v.number, seq->v.number,
                request->v.string, level->v.number, strm->v.string, target->v.string, node->v.string, delay_diff.tv_sec, delay_diff.tv_nsec);
    }
    else
        sprintf(reply_str,"[nodeid %.0f session %.0f seq %.0f]\t%s level %.0f on stream %s target %s\treply from %s",
                nid->v.number, sess->v.number, seq->v.number,
                request->v.string, level->v.number, strm->v.string, target->v.string, node->v.string);


    struct JsonValue *jrr = json_object_get_array(j, "rr");
    rr_str[0] = 0;
    if(jrr){
        sprintf(rr_str, "Record Route: [");
        REVERSE_LIST(jrr->v.array);
        for (struct JsonArray *a = jrr->v.array; a; a = a->next) {
            strcat(rr_str, " ");
            strcat(rr_str, a->val->v.string);
        }
        strcat(rr_str, " ]");
    }

    struct JsonValue *jos = json_object_get_object(j, "objects");
    if(jos){
        // ToDo: formatted printout per object type
        sprintf(obj_str, "\tObject ");
        struct JsonValue *val = json_object_get_string(jos, "name");
        if(val == NULL) {
            fprintf(stderr, "No name in object in reply.\n");
            return -1;
        }
        strcat(obj_str, val->v.string);
        strcat(obj_str, " type ");
        val = json_object_get_string(jos, "type");
        if(val == NULL) {
            fprintf(stderr, "No type in object in reply.\n");
            return -1;
        }
        strcat(obj_str, val->v.string);

        // dump according to the type
        if(strcmp(val->v.string,"seqgen")==0){
            dump_seqgen_state(obj_str, jos);
        }
        else if(strcmp(val->v.string,"seqrec")==0){
            dump_seqrec_state(obj_str, jos);
        }
        else if(strcmp(val->v.string,"replicate")==0){
            dump_repl_state(obj_str, jos);
        }
        else if(strcmp(val->v.string,"pof")==0){
            dump_pof_state(obj_str, jos);
        }
        else {  // unknown type, just dump
            unsigned jos_length;
            char *jos_string = json_serialize(jos, &jos_length);
            strcat(obj_str, jos_string);
            free(jos_string);
        }
    }
    json_delete(j);

    // Logging
    log_info(OAM, "%s %s\n", reply_str, rr_str);

    if(oam_cmd_iface == NULL)
        return -1;
    else
    {
        if(cfg_mode) return -1;         // silent, no reply needed

        if(oam_cmd_get_mode(oam_cmd_iface) == JSON){           // JSON mode
            //strcat(msg, "\n"); //TODO this is buffer overflow
            return oam_cmd_recv_reply(oam_cmd_iface, msg);
        } else {                                               // DUMP mode
            if (rr_str[0]) {
                strcat(reply_str, "\n\t");
                strcat(reply_str, rr_str);
                strcat(reply_str, "\n");
            }
            if(jos)
                strcat(reply_str, obj_str);
            //TODO print reply to session->cmd_w
            return oam_cmd_recv_reply(oam_cmd_iface, reply_str);
        }
    }
}

/*
 * Send UDP OAM reply mesage
 * Address: destination address string (can be either IPV4, IPv6, or FQDN)
 * Msg: pointer to the message
 * Return 0 on success
*/
int oam_send_reply(const char *address, unsigned port, const char *msg, unsigned msg_len)
{
  struct addrinfo hints, *res, *rp;
  int status;

  char port_str[15];
  sprintf(port_str, "%u", port);
  bzero(&hints, sizeof(hints));
  hints.ai_family = PF_UNSPEC;     // can be ipv4 or ipv6
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_PASSIVE;
  if ((status = getaddrinfo(address, port_str, &hints, &res)) != 0) {
    fprintf(stderr, "oam_send_reply getaddrinfo for address '%s': %s\n", address, gai_strerror(status));
    return -1;
  }

  int sock = -1;
  for (rp=res; rp!=NULL; rp=rp->ai_next) {
      sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
      if (sock < 0) continue;

      //TODO if getaddrinfo() returns multiple items, this will send to each one
    if (sendto(sock, msg, msg_len, 0, rp->ai_addr, rp->ai_addrlen) == 0) {
        perror("oam repy sendto");
        freeaddrinfo(res);
        close(sock);
        return -1;
    }
  }

  freeaddrinfo(res);
  close(sock);
  return 0;
}


static int oam_start_ping_cb(const char *key, void *value, void *userdata)
{
    (void) userdata;
    (void) key;
    struct ConfOam *oam = value;
    printf("starting command: %s\n", oam->name);

    struct oam_request *ping_req = NULL;

    if( (ping_req = oam_parse_ping(oam->request, OAM_CFG, stderr)) == NULL)
        return -EINVAL;

    ping_req->count = 0;    // force infinite count

    return (oam_ping(ping_req) == 0);
}

// Initialize OAM functionality
bool init_oam(struct R2d2Config *config)
{
    printf("Init OAM fuctionality.\n");
    //TODO what should this function do?

    pthread_mutex_init(&session_lock, NULL);
    session_ids = new_hashmap(11, NULL, NULL);

    // Start OAM background streams
    if (!hashmap_foreach(config->oam, oam_start_ping_cb, NULL)) {
        fprintf(stderr, "failed to start oam command\n");
        return NULL;
    }
    return true;
}

// Close OAM functionality
bool close_oam(void)
{
    struct ListParams params = {NULL};
    hashmap_foreach(session_ids, close_sessions_cb, &params);
    return true;
}

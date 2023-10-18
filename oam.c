// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.

#include "oam.h"
#include "conf_oam.h"
#include "conf_object.h"
#include "hashmap.h"
#include "if_oam.h"
#include "if_oam_cmd.h"
#include "interface.h"
#include "json.h"
#include "log.h"
#include "packet.h"
#include "pipeline.h"
#include "time_utils.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <math.h>

#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <netdb.h>

#define OAM_RCVY_RESET_MS 5000
#define OAM_PING_TTL 64
#define OAM_CHANNEL 1 /* Management Communication Channel (MCC), similar format to ours */

DEFAULT_LOGGING_MODULE(OAM, LOG_INFO);

struct MepStart {
    char *name;
    char *mep_name;
    char *stream_name;
    struct Pipeline *pipe;
    int pipe_pos_idx;
    int level;
};

struct oam_request{
    FILE *cmd_w;
    const char *return_ip;
    unsigned return_port;
    unsigned session_id, seq;
    unsigned short node_id;
    const char *type;
    struct MepStart *mep_start;
    char mep_stop[32];               // destination mep/mip
    int level;
    bool record_route;
    bool object_state;
    bool delay;
    unsigned count;
    unsigned interval_ms;
    unsigned char ttl;
    char *remote_command; // for rping
    char *originator_stream; // for ping initiated by rping
    unsigned originator_session_id; // for ping initiated by rping
};

static int nr_oam_ifaces = 0;
static struct Interface *oam_ifaces[16];
static struct Interface *oam_default_iface = NULL;
static struct Interface *oam_cmd_iface = NULL;
static struct HashMap *mep_starts = NULL; // name -> struct MEPStart

struct SessionTracker {
    FILE* cmd_w; //TODO this is not good if the cmd connection closes before we get the reply
    time_t access_time;
    unsigned interval_ms;
    pthread_t multireq_tid;
    struct oam_request *req;
    bool live;
};

static char *last_stream=NULL; // the stream name of the last issued command
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

    while (stream->sessions[id].live) {
        unsigned timeout = MAX(ceil(1.0 + 0.001*stream->sessions[id].interval_ms), 2);
        if (now.tv_sec > stream->sessions[id].access_time + timeout) {
            //fprintf(stderr, "session %u timeouted\n", id);
            stream->sessions[id].live = false;
            stream->sessions[id].multireq_tid = 0;
            break;
        }
        id = (id + 1) % 16;
        if (id == next_id) break;
    }

    if (stream->sessions[id].live) {
        pthread_mutex_unlock(&session_lock);
        return -1;
    } else {
        stream->last_session = id;
        stream->sessions[id].live = 1;
        stream->sessions[id].access_time = now.tv_sec + 1;
        stream->sessions[id].req = req;
        stream->sessions[id].multireq_tid = 0;
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
    mepstart->name = strdup_printf("%s:%s", stream_name, mep_name);
    mepstart->mep_name = strdup(mep_name);
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

struct OamEndPoint *oam_create_endpoint(const char *name, const char *stream, int level, struct ConfObject *target, bool stop)
{
    struct OamEndPoint *ret = calloc_struct(OamEndPoint);
    ret->name = strdup(name);
    ret->stream = strdup(stream);
    ret->level = level;
    ret->target = target;
    ret->stop = stop;
    return ret;
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

// returns true on success
static bool send_request(const struct oam_request *req){
    struct Packet *packet = new_packet(NULL);

    unsigned session_id = req->originator_stream ? req->originator_session_id : req->session_id;
    add_fixed_headers(packet, req->ttl, req->seq, OAM_CHANNEL,
                      req->node_id, req->level, session_id);
    packet->ttl = req->ttl;

    struct JsonValue *js = json_object();
    json_object_insert(js, "request", json_string(req->type));
    json_object_insert(js, "req_type", json_string("request"));
    if (req->originator_stream) {
        json_object_insert(js, "stream", json_string(req->originator_stream));
    } else {
        json_object_insert(js, "stream", json_string(req->mep_start->stream_name));
    }
    json_object_insert(js, "target", json_string(req->mep_stop));
    struct JsonValue *jret = json_object();
    json_object_insert(jret, "ip", json_string(req->return_ip));
    json_object_insert(jret, "port", json_number(req->return_port));
    json_object_insert(js, "return", jret);

    if(strcmp(req->type, "ping")==0){
        if(req->record_route){
            struct JsonValue *jrr = json_array();
            json_array_unshift(jrr, json_string(req->mep_start->name));
            json_object_insert(js, "rr", jrr);
        }
        if(req->object_state){
            json_object_insert(js, "object", json_true());
        }
        if(req->delay){
            json_object_insert(js, "delay", json_true());
        }
    }
    else if(strcmp(req->type, "rping")==0){
        //TODO this hardcodes the commandline format into the protocol :(
        json_object_insert(js, "command", json_string(req->remote_command));
    }

    struct timespec sendtime;
    clock_gettime(CLOCK_REALTIME, &sendtime);
    packet->recv_time = sendtime;

    json_object_insert(js, "send_s", json_number(sendtime.tv_sec));
    json_object_insert(js, "send_ns", json_number(sendtime.tv_nsec));

    unsigned js_length;
    char *js_string;
    js_string = json_serialize(js, &js_length);
    json_delete(js);
    if (js_string == NULL) {
        //TODO can this happen?
        delete_packet(packet);
        return false;
    }
    packet_add_header(packet, 2, PROTO_ID_PAYLOAD, js_length);
    unsigned char *msg = packet->buf + packet->headers[2].start;
    memcpy(msg, js_string, js_length);

    log_packet("%s %s:%d seq %d lvl %d S - %s",
               req->mep_start->mep_name, req->mep_start->stream_name, req->session_id, req->seq, req->level,
               js_string);

    free(js_string);

    struct PipelineIterator *pi = new_pipe_iterator(req->mep_start->pipe, packet);
    pi->pos = req->mep_start->pipe_pos_idx;

    pipe_iterator_run(pi);
    return true;
}

static void *oam_request_thread(void *arg)
{
    struct oam_request *req = (struct oam_request *)arg;
    unsigned seq=0;
    struct StreamSessions *stream = hashmap_find(session_ids, req->mep_start->stream_name);
    stream->sessions[req->session_id].interval_ms = req->interval_ms;

    while(1){
        if((req->count != 0) && (seq >= req->count)) break;
        req->seq = seq & 0xFF;
        send_request(req);
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        stream->sessions[req->session_id].access_time = now.tv_sec + 1;
        usleep(req->interval_ms * 1000);
        seq++;
    }
    stream->sessions[req->session_id].live = false;
    stream->sessions[req->session_id].multireq_tid = 0;
    free(req);
    return NULL;
}

// returns true on success
static bool initiate_request(struct oam_request *ping_req)
{
    FILE *cmd_w = ping_req->cmd_w;

    struct Pipeline *pipe = ping_req->mep_start->pipe;
    if (!pipe) {
        fprintf(cmd_w, "mep start '%s' has no pipeline!?!\n", ping_req->mep_start->name);
        return false;
    }

    int session_id = alloc_session_id(ping_req->mep_start->stream_name, ping_req, cmd_w);
    if (session_id < 0) {
        fprintf(cmd_w, "mep start '%s' has no free session id\n", ping_req->mep_start->name);
        return false;
    }

    ping_req->session_id = session_id;
    ping_req->seq = 0;

    log_info("  %s %s:%d seq %d lvl %d C - %s %s -> %s, level %d, count %d interval %d, rr: %s os: %s\t[reply to ip: %s, port: %u]",
             ping_req->mep_start->mep_name, ping_req->mep_start->stream_name, ping_req->session_id, ping_req->seq, ping_req->level, ping_req->type, ping_req->mep_start->name, ping_req->mep_stop, ping_req->level, ping_req->count, ping_req->interval_ms,
             ping_req->record_route?"yes":"no", ping_req->object_state?"yes":"no", ping_req->return_ip, ping_req->return_port);

    fprintf(cmd_w, "OAM packet %s session %u seq %u, %s -> %s, level %d, count %d interval %d, rr: %s os: %s\t[reply to ip: %s, port: %u]\n",
            ping_req->type, ping_req->session_id, ping_req->seq, ping_req->mep_start->name, ping_req->mep_stop, ping_req->level, ping_req->count, ping_req->interval_ms,
            ping_req->record_route?"yes":"no", ping_req->object_state?"yes":"no", ping_req->return_ip, ping_req->return_port);

    struct StreamSessions *stream = hashmap_find(session_ids, ping_req->mep_start->stream_name);
    if(ping_req->count == 1){
        stream->sessions[session_id].multireq_tid = 0;
        send_request(ping_req);
        free(ping_req);
    } else {
        pthread_attr_t attr;
        if ((errno = pthread_attr_init(&attr)) != 0) {
            perror("oam ping thread pthread_attr_init");
            return false;
        }

        if (pthread_create(&stream->sessions[session_id].multireq_tid, &attr, &oam_request_thread, ping_req) != 0) {
            fprintf(stderr, "could not create new ping thread\n");
            return false;
        }
    }

    return true;
}

static struct oam_request *new_oam_request(const char *type, FILE *cmd_w)
{
    struct oam_request *req = calloc_struct(oam_request);

    req->cmd_w = cmd_w;
    req->type = type;
    req->ttl = OAM_PING_TTL;
    req->count = 1;
    req->interval_ms = 1000;

    return req;
}

static bool parse_ping_returnif(struct oam_request *ping_req, const char *ifname)
{
    struct Interface *iface = ifname[0] ? get_oam_if(ifname) : oam_default_iface;
    if (iface == NULL) {
        fprintf(ping_req->cmd_w, "invalid return interface name: %s", ifname);
        return false;
    }
    ping_req->node_id = oamif_get_uid(iface);
    ping_req->return_ip = oamif_get_ip(iface);
    ping_req->return_port = oamif_get_port(iface);
    return true;
}

static bool parse_ping_options(struct oam_request *ping_req, const char *options_str, bool allow_num)
{
    const char *po = options_str;
    bool opt_err = false;
    int k, l;
    int val;
    float fval;
    char c;

    while ((k=sscanf(po, " -%c%n", &c, &l)) == 1) {
        if (!isspace(*po)) {
            fprintf(ping_req->cmd_w, "Error: ping options must be separated by space\n");
            opt_err = true;
            break;
        }
        po += l;
        if (c=='r') {
            ping_req->record_route = true;
        } else if (c=='o') {
            ping_req->object_state = true;
        } else if (c=='d') {
            ping_req->delay = true;
        } else if (c=='i') {
            k = sscanf(po, " %f%n", &fval, &l);
            if (k == 1) {
                po += l;
                if (fval < 0.002) fval = 0.002; // 2msec is the minimum
                ping_req->interval_ms = fval * 1000;
            } else {
                fprintf(ping_req->cmd_w, "Error: ping interval is invalid\n");
                opt_err = true;
                break;
            }
        } else if (c=='n') {
            if(!allow_num){
                fprintf(ping_req->cmd_w, "Error: ping count is not allwed in config.\n");
                opt_err = true;
                break;
            }
            k = sscanf(po, " %d%n", &val, &l);
            if (k == 1) {
                po += l;
                ping_req->count = val;
            } else {
                fprintf(ping_req->cmd_w, "Error: ping count is invalid\n");
                opt_err = true;
                break;
            }
        } else if (c=='t') {
            k = sscanf(po, " %d%n", &val, &l);
            if (k == 1) {
                po += l;
                ping_req->ttl = val;
            } else {
                fprintf(ping_req->cmd_w, "Error: ping ttl is invalid\n");
                opt_err = true;
                break;
            }
        } else {
            fprintf(ping_req->cmd_w, "Error: ping option '%c' is invalid\n", c);
            opt_err = true;
            break;
        }
    }
    if (opt_err) return false;
    while (isspace(*po)) po++;
    if (*po) {
        fprintf(ping_req->cmd_w, "ping options '%s' is invalid", po);
        return false;
    }
    return true;
}

static struct oam_request *parse_ping_command(const char *oam_command, bool allow_returniface, bool allow_num, FILE *cmd_w)
{
    int l;
    char start_name[32];
    char iface_name[32];

    struct oam_request *ping_req = new_oam_request("ping", cmd_w);

    if (oam_command[0]=='@') {
        if (!allow_returniface) {
            fprintf(cmd_w, "ping return interface is not allowed");
            free(ping_req);
            return NULL;
        }
        int k = sscanf(oam_command, "@%s %s %s %d%n",
                       iface_name, start_name, ping_req->mep_stop, &ping_req->level, &l);
        if (k < 4) {
            fprintf(cmd_w, "ping arguments invalid");
            free(ping_req);
            return NULL;
        }
    } else {
        int k = sscanf(oam_command, " %s %s %d%n",
                       start_name, ping_req->mep_stop, &ping_req->level, &l);
        if (k < 3) {
            fprintf(cmd_w, "ping arguments invalid");
            free(ping_req);
            return NULL;
        }
        iface_name[0] = 0;
    }

    if (!parse_ping_returnif(ping_req, iface_name)) {
        free(ping_req);
        return NULL;
    }

    ping_req->mep_start = hashmap_find(mep_starts, start_name);
    if (ping_req->mep_start == NULL) {
        fprintf(cmd_w, "ping start '%s' invalid\n", start_name);
        free(ping_req);
        return NULL;
    }

    if (!parse_ping_options(ping_req, oam_command+l, allow_num)) {
        free(ping_req);
        return NULL;
    }
    return ping_req;
}

static struct oam_request *parse_rping_command(const char *oam_command, FILE *cmd_w)
{
    int l;
    char start_name[32];
    char iface_name[32];

    struct oam_request *rping_req = new_oam_request("rping", cmd_w);

    if (oam_command[0]=='@') {
        int k = sscanf(oam_command, "@%s %s %s %d%n",
                       iface_name, start_name, rping_req->mep_stop, &rping_req->level, &l);
        if (k < 4) {
            fprintf(cmd_w, "rping arguments invalid");
            free(rping_req);
            return NULL;
        }
    } else {
        int k = sscanf(oam_command, " %s %s %d%n",
                       start_name, rping_req->mep_stop, &rping_req->level, &l);
        if (k < 3) {
            fprintf(cmd_w, "rping arguments invalid");
            free(rping_req);
            return NULL;
        }
        iface_name[0] = 0;
    }

    if (!parse_ping_returnif(rping_req, iface_name)) {
        free(rping_req);
        return NULL;
    }

    rping_req->mep_start = hashmap_find(mep_starts, start_name);
    if (rping_req->mep_start == NULL) {
        fprintf(cmd_w, "rping start '%s' invalid\n", start_name);
        free(rping_req);
        return NULL;
    }

    while (isspace(oam_command[l])) l++;
    rping_req->remote_command = strdup(oam_command+l);

    return rping_req;
}

static struct oam_request *parse_rlist_command(const char *oam_command, FILE *cmd_w)
{
    int l;
    char start_name[32];
    char iface_name[32];

    struct oam_request *rlist_req = new_oam_request("rlist", cmd_w);

    if (oam_command[0]=='@') {
        int k = sscanf(oam_command, "@%s %s %s %d%n",
                       iface_name, start_name, rlist_req->mep_stop, &rlist_req->level, &l);
        if (k < 4) {
            fprintf(cmd_w, "rlist arguments invalid");
            free(rlist_req);
            return NULL;
        }
    } else {
        int k = sscanf(oam_command, " %s %s %d%n",
                       start_name, rlist_req->mep_stop, &rlist_req->level, &l);
        if (k < 3) {
            fprintf(cmd_w, "rlist arguments invalid");
            free(rlist_req);
            return NULL;
        }
        iface_name[0] = 0;
    }

    if (!parse_ping_returnif(rlist_req, iface_name)) {
        free(rlist_req);
        return NULL;
    }

    rlist_req->mep_start = hashmap_find(mep_starts, start_name);
    if (rlist_req->mep_start == NULL) {
        fprintf(cmd_w, "rlist start '%s' invalid\n", start_name);
        free(rlist_req);
        return NULL;
    }

    while (isspace(oam_command[l])) l++;
    if (oam_command[l]) {
        fprintf(cmd_w, "rlist doesn't take so many arguments\n");
        free(rlist_req);
        return NULL;
    }

    return rlist_req;
}

static const char help_str[] =
    "Available commands:\n"
    "help - get help\n"
    "exit - exit OAM\n"
    "mode <mode> - terminal mode. Mode can be 'dump' or 'json'.\n"
    "log [module newlevel] - get current log levels or set it for the given module.\n"
    "list - list monitoring start points\n"
    "rlist[@if] <stream:mep-start/mip> <mep-stop/mip/any> <level> - list monitoring start points of the remote node.\n"
    "sessions [stream] - list active sessions for 'stream'. List all sessions if no 'stream' specified.\n"
    "stop [stream session_id] - stop a running OAM session, identified by stream:session_id. Stops the last session if no parameters given.\n"
    "returns - list return interfaces\n"
    "ping[@if] <stream:mep-start/mip> <mep-stop/mip/any> <level> [-r] [-o] [-i <interval>] [-n <count>] [-t <ttl>]\n"
    "rping[@if] <stream:mep-start/mip> <mep-stop/mip> <level> <remote stream:mep-start/mip> <remote mep-stop/mip/any> <remote level> [-r] [-o] [-i <interval>] [-n <count>] [-t <ttl>]\n";

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
            fprintf(params->cmd_w,"\t%s:%d\t %s %s -> %s level %d\n", key, i, req->type, req->mep_start->name, req->mep_stop, req->level);
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
            if (stream->sessions[session].multireq_tid > 0) {
                if (pthread_cancel(stream->sessions[session].multireq_tid) <  0) {
                    fprintf(cmd_w," - failed.\n");
                } else {
                    fprintf(cmd_w," - stopped.\n");
                }
            }
            stream->sessions[session].live = false;
            stream->sessions[session].multireq_tid = 0;
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

static int list_log_modules_cb(const char *mod_name, LOGGING_LEVELS current_level, void *userdata)
{
    FILE *cmd_w = userdata;
    fprintf(cmd_w, "  %s level %s\n", mod_name, log_string_from_level(current_level));
    return 1;
}

#define TELNET_IAC         0xff /* Interpret As Command */
#define TELNET_INTERRUPT   0xf4 /* interrupt process */
#define TELNET_WILL        0xfb
#define TELNET_WONT        0xfc
#define TELNET_DO          0xfd
#define TELNET_DONT        0xfe
#define TELNET_TIMING_MARK    6 /* see RFC 860 */
static void handle_telnet_command(unsigned char *oam_command, int *n, FILE *cmd_w)
{
    int k = 0;
    bool stop = false;

    while (oam_command[k] == TELNET_IAC) {
        if (*n > k+1) {
            if (oam_command[k+1] == TELNET_INTERRUPT) {
                stop = true;
                k += 2;
            } else if (oam_command[k+1] == TELNET_DO) {
                if (*n > k+2) {
                    if (oam_command[k+2] == TELNET_TIMING_MARK) {
                        // we must reply with this or the telnet client gets stuck (see RFC 860)
                        char reply[] = {TELNET_IAC, TELNET_WILL, TELNET_TIMING_MARK, 0};
                        fprintf(cmd_w, "%s", reply);
                        k += 3;
                    } else {
                        fprintf(stderr, "unhandled telnet DO command %d\n", oam_command[k+2]);
                        k += 3;
                    }
                } else {
                    fprintf(stderr, "incomplete 2 byte telnet DO command %d received\n", oam_command[k+2]);
                    k += 2;
                }
            } else {
                fprintf(stderr, "unhandled telnet command %d\n", oam_command[k+1]);
                k += 2; //TODO we don't know how long this command is
            }
        } else {
            fprintf(stderr, "incomplete 1 byte telnet command received\n");
            k += 1;
        }
        if (k >= *n) break;
    }

    // remove the processed commands by moving everything forward
    if (k > 0) {
        memmove(oam_command, oam_command+k, *n-k+1); // include the closing 0
        *n -= k;
    }

    // interpret the interrupt command (ctrl+C) as "stop"
    if (stop) {
        memmove(oam_command+4, oam_command, *n+1); // include the closing 0
        const char *stop_s = "stop";
        memcpy(oam_command, stop_s, 4);
        *n += 4;
    }
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
        int n = read(cmd_fd, oam_command, sizeof(oam_command)-1);
        if (n > 0) {
            oam_command[n] = 0;

            if ((unsigned char)(oam_command[0]) == TELNET_IAC) {
                handle_telnet_command((unsigned char*)oam_command, &n, cmd_w);
                if (n == 0) continue;
            }

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
            else if (strncmp(oam_command, "log", 3) == 0) {
                char modulename[64];
                char newlevel[16];
                k = sscanf(oam_command, "log %s %s", modulename, newlevel);
                if (k == 0 || k == EOF) {
                    fprintf(cmd_w, "Logging modules:\n");
                    log_foreach_modules(list_log_modules_cb, cmd_w);
                } else if (k == 2) {
                    LOGGING_LEVELS nlvl = log_level_from_string(newlevel);
                    if (!log_set_level(modulename, nlvl)) {
                        fprintf(cmd_w, "Module '%s' does not exist.\n", modulename);
                    } else {
                        fprintf(cmd_w, "Module '%s' new level %s.\n", modulename, log_string_from_level(nlvl));
                    }
                } else {
                    fprintf(cmd_w, "Invalid parameters for 'log' command.\n");
                }
            }
            else if (strncmp(oam_command, "sessions", 8) == 0) {
                struct ListParams params = {cmd_w};
                k=sscanf(oam_command, "sessions %s%n", streamname, &l);
                if(k==0 || k==EOF){
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
                if(k==0 || k==EOF){
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
            else if (strncmp(oam_command, "ping", 4) == 0) {
                struct oam_request *ping_req = parse_ping_command(oam_command+4, true, true, cmd_w);
                if (ping_req == NULL) {
                    ERROR("ping command is invalid");
                }
                char *req_stream = ping_req->mep_start->stream_name;
                if (!initiate_request(ping_req)) {
                    ERROR("sending ping command failed");
                }
                last_stream = req_stream; //TODO this is not thread-safe
            }
            else if (strncmp(oam_command, "rping", 5) == 0) {
                struct oam_request *rping_req = parse_rping_command(oam_command+5, cmd_w);
                if (rping_req == NULL) {
                    ERROR("rping command is invalid");
                }
                if (!initiate_request(rping_req)) {
                    ERROR("sending rping command failed");
                }
            }
            else if (strncmp(oam_command, "rlist", 5) == 0) {
                struct oam_request *rlist_req = parse_rlist_command(oam_command+5, cmd_w);
                if (rlist_req == NULL) {
                    ERROR("rlist command is invalid");
                }
                if (!initiate_request(rlist_req)) {
                    ERROR("sending rlist command failed");
                }
            }
            else {
                ERROR("unknown command '%s'", oam_command);
            }
        }
        else {
            if (n < 0) {
                perror("read");
            }
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
static int dump_seqgen_state(char *str, struct JsonValue *jos, const char *record_sep, const char *line_sep){
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
    snprintf(tmp, sizeof(tmp), "use_init_flag: %s%s use_reset_flag: %s%s", (ini->type == JSON_TRUE)? "true":"false", record_sep, (rst->type == JSON_TRUE)? "true":"false", line_sep);
    strcat(str, tmp);

    return 0;
}

static int dump_seqrec_state(char *str, struct JsonValue *jos, const char *record_sep, const char *line_sep){
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
    if (strcmp(algo->v.string, "match") != 0) {
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
    const char *fmt_match = "recovery_algorithm: %s%s reset_timer: %.0fms%s" \
        "latest_valid_sequence_number: %.0f%s passed: %.0f%s discarded: %.0f%s" \
        "number_of_resets: %.0f%s";

    const char *fmt_vector = "recovery_algorithm: %s%s use_init_flag: %s%s use_reset_flag: %s%s" \
        "reset_timer: %.0fms%s history_length: %.0f%s" \
        "latest_valid_sequence_number: %.0f%s passed: %.0f%s discarded: %.0f%s" \
        "history_content: %s%s" \
        "latent_error_paths: %.0f%s latent_error_resets: %.0f%s"
        "number_of_resets: %.0f%s";
    if (vector) {
        bool init = init_flag->type == JSON_TRUE ? true : false;
        bool reset = reset_flag->type == JSON_TRUE ? true : false;
        snprintf(tmp, sizeof(tmp), fmt_vector, algo->v.string, record_sep, init ? "true" : "false", record_sep, reset ? "true" : "false", line_sep,
                 reset_msec->v.number, record_sep, hist_len->v.number, line_sep,
                 seq->v.number, record_sep, passed->v.number, record_sep, discarded->v.number, line_sep,
                 hist->v.string, line_sep,
                 err_paths->v.number, record_sep, errs->v.number, line_sep,
                 resets->v.number, line_sep);
    } else {
        snprintf(tmp, sizeof(tmp), fmt_match, algo->v.string, record_sep, reset_msec->v.number, line_sep,
                 seq->v.number, record_sep, passed->v.number, record_sep, discarded->v.number, line_sep,
                 resets->v.number, line_sep);
    }
    strcat(str, tmp);
    return 0;
}

static int dump_repl_state(char *str, struct JsonValue *jos, const char *record_sep, const char *line_sep){
    (void)record_sep;
    char tmp[128];
    struct JsonValue *pass = json_object_get_number(jos, "packets_passed");
    if(pass == NULL) {
        fprintf(stderr, "No packets_passed in object in reply.\n");
        return -1;
    }
    snprintf(tmp, sizeof(tmp), "packets_passed: %.0f%s", pass->v.number, line_sep);
    strcat(str, tmp);

    return 0;
}

static int dump_pof_state(char *str, struct JsonValue *jos, const char *record_sep, const char *line_sep){
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
    const char *fmt = "max_buffer_length: %.0f%s max_delay: %.0fms%s take_any_time: %.0fms%s" \
        "current_buffer_length: %.0f%s last_sent: %.0f%s";
    snprintf(tmp, sizeof(tmp), fmt, buff_size->v.number, record_sep, max_delay->v.number, record_sep, take_any_time->v.number, line_sep,
              buff_len->v.number, record_sep, last_sent->v.number, line_sep);
    strcat(str, tmp);
    return 0;
}

static int dump_object_state(char *str, struct JsonValue *jos, const char *record_sep, const char *line_sep){
    char tmp[500] = { 0 };

    sprintf(tmp, "Object ");
    struct JsonValue *oname = json_object_get_string(jos, "name");
    strcat(tmp, oname->v.string);
    strcat(tmp, " type ");
    struct JsonValue *type = json_object_get_string(jos, "type");
    strcat(tmp, type->v.string);
    strcat(tmp, line_sep);

    // dump according to the type
    if(strcmp(type->v.string,"seqgen")==0){
        dump_seqgen_state(tmp, jos, record_sep, line_sep);
    }
    else if(strcmp(type->v.string,"seqrec")==0){
        dump_seqrec_state(tmp, jos, record_sep, line_sep);
    }
    else if(strcmp(type->v.string,"replicate")==0){
        dump_repl_state(tmp, jos, record_sep, line_sep);
    }
    else if(strcmp(type->v.string,"pof")==0){
        dump_pof_state(tmp, jos, record_sep, line_sep);
    }
    else {  // unknown type, just dump
        unsigned jos_length;
        char *jos_string = json_serialize(jos, &jos_length);
        strcat(tmp, jos_string);
        free(jos_string);
    }
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
#define JS_OBJECT_GET(_key, _type, _json)                           \
    struct JsonValue *_key = json_object_get_##_type(_json, #_key); \
    if (_key == NULL) {                                             \
        log_error("No " #_key " in reply.");                        \
        json_delete(j);                                             \
        return -1;                                                  \
    }

    char reply_str[1400], rr_str[512], obj_str_log[1000], obj_str[1000];
    struct JsonValue *j = json_parse(msg, strlen(msg));
    if (j == NULL || j->type != JSON_OBJECT) {
        //fprintf(stderr, "JSON in reply is invalid.\n");
        log_error("JSON in reply is invalid.");
        return -1;
    }
    JS_OBJECT_GET(nodeid, number, j);
    JS_OBJECT_GET(request, string, j);
    JS_OBJECT_GET(target, string, j);
    JS_OBJECT_GET(sequence, number, j);
    JS_OBJECT_GET(level, number, j);
    JS_OBJECT_GET(receiver, string, j);
    JS_OBJECT_GET(stream, string, j);
    JS_OBJECT_GET(session, number, j);

    struct SessionTracker *sess = NULL;
    if (session->v.number < 0 || session->v.number > 15) {
        log_error("session id %.0f in reply is invalid", session->v.number);
        return -1;
    } else {
        struct StreamSessions *ss = hashmap_find(session_ids, stream->v.string);
        if (ss == NULL) {
            log_error("Unknown stream name '%s' in reply.", stream->v.string);
            //return -1;
        } else {
            sess = &ss->sessions[(int)(session->v.number)];
            if (!sess->live) {
                log_error("Reply for non-live session %.0f of stream '%s'.", session->v.number, stream->v.string);
                sess = NULL;
                //return -1;
            }
        }
    }

    log_packet("oam_r %s:%.0f seq %.0f lvl %.0f D - %s", stream->v.string, session->v.number, sequence->v.number, level->v.number, msg);

    //TODO this is just a quick fix
    if (strcmp(request->v.string, "rlist") == 0) {
        JS_OBJECT_GET(list, array, j);
        sprintf(reply_str, "Rlist result from %s:\n", receiver->v.string);
        for (struct JsonArray *l = list->v.array; l; l=l->next) {
            if (l->val->type != JSON_STRING) {
                log_error("rlist result is not string.");
                json_delete(j);
                return -1;
            }
            strcat(reply_str, l->val->v.string);
            strcat(reply_str, "\n");
        }
        return oam_cmd_recv_reply(oam_cmd_iface, reply_str);
    }

    struct JsonValue *dly = json_object_get_bool(j, "delay");
    if(dly != NULL && dly->type == JSON_TRUE){
        // calculate delay
        struct timespec sendtime, receivetime, delay_diff;
        sendtime.tv_sec = json_object_get_number(j, "send_s")->v.number;
        sendtime.tv_nsec = json_object_get_number(j, "send_ns")->v.number;
        receivetime.tv_sec = json_object_get_number(j, "recv_s")->v.number;
        receivetime.tv_nsec = json_object_get_number(j, "recv_ns")->v.number;
        timespecsub(&receivetime, &sendtime, &delay_diff);

        sprintf(reply_str,"  oam_r %s:%.0f seq %.0f lvl %.0f R - %s on stream %s target %s; reply from %s delay %ld.%ld",
                stream->v.string, session->v.number, sequence->v.number, level->v.number,
                request->v.string, stream->v.string, target->v.string, receiver->v.string, delay_diff.tv_sec, delay_diff.tv_nsec);
    }
    else
        sprintf(reply_str,"  oam_r %s:%.0f seq %.0f lvl %.0f R - %s on stream %s target %s; reply from %s",
            stream->v.string, session->v.number, sequence->v.number, level->v.number,
            request->v.string, stream->v.string, target->v.string, receiver->v.string);

    // Recorded route is single line, no difference between log/dump
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

    // different format between log/dump
    obj_str[0] = 0; obj_str_log[0] = 0;
    struct JsonValue *jos = json_object_get_object(j, "object");
    if (jos && jos->type == JSON_OBJECT){
        dump_object_state(obj_str, jos, ",", "\n\t\t");
        dump_object_state(obj_str_log, jos, ",", "; ");
    }

    // Logging
    log_info("%s %s %s", reply_str, rr_str, obj_str_log);

    json_delete(j);

    if(oam_cmd_iface == NULL)
        return -1;
    else
    {
        // check if we need to print to a telnet session
        if (sess == NULL) return 0; // no session found
        if (sess->cmd_w == stderr) return 0; // this is a background ping

        if(oam_cmd_get_mode(oam_cmd_iface) == JSON){           // JSON mode
            //strcat(msg, "\n"); //TODO this is buffer overflow
            return oam_cmd_recv_reply(oam_cmd_iface, msg);
        } else {                                               // DUMP mode
            if (rr_str[0]) {
                strcat(reply_str, "\n\t");
                strcat(reply_str, rr_str);
            }
            if(jos){
                strcat(reply_str, "\n\t");
                strcat(reply_str, obj_str);
            }
            //TODO print reply to session->cmd_w
            return oam_cmd_recv_reply(oam_cmd_iface, reply_str);
        }
    }
    #undef JS_OBJECT_GET
}

/*
 * Send UDP OAM reply mesage
 * Address: destination address string (can be either IPV4, IPv6, or FQDN)
 * Msg: pointer to the message
 * Return 0 on success
*/
static int oam_send_reply(const char *address, unsigned port, const char *msg, unsigned msg_len)
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

        if (sendto(sock, msg, msg_len, 0, rp->ai_addr, rp->ai_addrlen) == 0) {
            perror("oam repy sendto");
            freeaddrinfo(res);
            close(sock);
            return -1;
        } else
        break;
    }

    freeaddrinfo(res);
    close(sock);
    return 0;
}

// @returns false on error
static bool get_return_ip_port(struct JsonValue *j, char **reply_address, int *port)
{
    struct JsonValue *jret = json_object_get_object(j, "return");
    if (jret==NULL) {
        fprintf(stderr, "OAM packet has no return address\n");
        return false;
    }

    struct JsonValue *val = json_object_get_number(jret, "port");
    if(val!=NULL)
        *port=val->v.number;
    else {
        return false;
    }
    val = json_object_get_string(jret, "ip");
    if(val!=NULL)
        *reply_address = strdup(val->v.string);
    else {
        return false;
    }

    return true;
}

// turns the request Json into a reply
// @returns false on error
static bool process_ping_request(struct OamEndPoint *oam, struct Packet *p, struct JsonValue *j)
{
    int port=6634;
    char *reply_address=NULL;

    unsigned char *oam_hdr = p->buf + p->headers[1].start;
    unsigned char seq = oam_hdr[1];
    //unsigned short channel = (oam_hdr[2]<<8)+oam_hdr[3];
    unsigned short nodeid = (oam_hdr[4]<<8)+oam_hdr[5];
    unsigned char level = oam_hdr[6] >> 1;
    unsigned char session = oam_hdr[7] & 0x0f;

    if (!get_return_ip_port(j, &reply_address, &port)) {
        json_delete(j);
        return false;
    }

    // if object state is requested
    struct JsonValue *jos = json_object_get_any(j, "object");
    if(jos!=NULL){
        if (oam->target && oam->target->print_state) {
            struct JsonValue *objinfo = oam->target->print_state(oam->target->object);
            json_object_insert(objinfo, "name", json_string(oam->target->name));
            json_object_insert(j, "object", objinfo);
        }
    }

    json_object_remove(j, "return");
    json_object_insert(j, "req_type", json_string("response"));
    json_object_insert(j, "sequence", json_number(seq));
    json_object_insert(j, "level", json_number(level));
    json_object_insert(j, "nodeid", json_number(nodeid));
    json_object_insert(j, "session", json_number(session));
    json_object_insert(j, "receiver", json_string(oam->name));

    const char *stream = "<unknown>";
    struct JsonValue *jstream = json_object_get_string(j, "stream");
    if (stream != NULL) {
        stream = jstream->v.string;
    }
    // we know that header 0 contains the label in the first 20 bit
    uint32_t *label = (uint32_t *) (p->buf + p->headers[0].start);
    json_object_insert(j, "label", json_number((ntohl(*label) >> 12) & 0xFFFFF));

    json_object_insert(j, "recv_s", json_number(p->recv_time.tv_sec));
    json_object_insert(j, "recv_ns", json_number(p->recv_time.tv_nsec));

    unsigned msg_len=0;
    char *j_msg = json_serialize(j, &msg_len);

    log_packet("%s %s:%d seq %d lvl %d T - (to %s %d) %s", oam->name, stream, session, seq, level,
               reply_address, port, j_msg);

    oam_send_reply(reply_address, port, j_msg, msg_len);
    free(reply_address);
    free(j_msg);

    json_delete(j);
    return true;
}

// @returns false on error
static bool process_rping_request(struct OamEndPoint *oam, struct Packet *p, struct JsonValue *j)
{
    int port=6634;
    char *reply_address=NULL;

    unsigned char *oam_hdr = p->buf + p->headers[1].start;
    //unsigned char seq = oam_hdr[1];
    //unsigned short channel = (oam_hdr[2]<<8)+oam_hdr[3];
    //unsigned short nodeid = (oam_hdr[4]<<8)+oam_hdr[5];
    //unsigned char level = oam_hdr[6] >> 1;
    unsigned char session = oam_hdr[7] & 0x0f;

    if (!get_return_ip_port(j, &reply_address, &port)) {
        json_delete(j);
        return false;
    }

    struct JsonValue *cmd = json_object_get_string(j, "command");

    struct oam_request *ping_req = parse_ping_command(cmd->v.string, false, true, stderr);
    if (ping_req) {
        if (strcmp(oam->stream, ping_req->mep_start->stream_name) != 0) {
            fprintf(stderr, "OAM remote ping request has invalid start stream name '%s', oam point is in stream '%s'\n",
                    ping_req->mep_start->stream_name, oam->stream);
            free(reply_address);
            json_delete(j);
            return false;
        }
        ping_req->return_ip = reply_address;
        ping_req->return_port = port;

        struct JsonValue *jstream = json_object_get_string(j, "stream");
        if (jstream == NULL) {
            fprintf(stderr, "OAM remote ping request has no stream name\n");
            free(reply_address);
            json_delete(j);
            return false;
        }
        ping_req->originator_stream = strdup(jstream->v.string);
        ping_req->originator_session_id = session;

        if (!initiate_request(ping_req)) {
            fprintf(stderr, "OAM sending remote ping request failed\n");
            free(reply_address);
            json_delete(j);
            return false;
        }
    } else {
        fprintf(stderr, "OAM rping request invalid\n");
        free(reply_address);
        json_delete(j);
        return false;
    }
    free(reply_address);
    json_delete(j);
    return false;
}

struct AddstartState {
    struct JsonValue *jlist;
    struct OamEndPoint *oam;
};
static int addstart_cb(const char *key, void *value, void *userdata)
{
    struct AddstartState *st = userdata;
    struct MepStart *mep = value;

    if (strcmp(mep->stream_name, st->oam->stream) == 0) {
        //TODO supply more info: level, type
        json_array_unshift(st->jlist, json_string(key));
    }

    return 1;
}

// @returns false on error
static bool process_rlist_request(struct OamEndPoint *oam, struct Packet *p, struct JsonValue *j)
{
    int port=6634;
    char *reply_address=NULL;

    unsigned char *oam_hdr = p->buf + p->headers[1].start;
    unsigned char seq = oam_hdr[1];
    //unsigned short channel = (oam_hdr[2]<<8)+oam_hdr[3];
    unsigned short nodeid = (oam_hdr[4]<<8)+oam_hdr[5];
    unsigned char level = oam_hdr[6] >> 1;
    unsigned char session = oam_hdr[7] & 0x0f;

    if (!get_return_ip_port(j, &reply_address, &port)) {
        json_delete(j);
        return false;
    }

    json_object_remove(j, "return");
    json_object_insert(j, "req_type", json_string("response"));
    json_object_insert(j, "sequence", json_number(seq));
    json_object_insert(j, "level", json_number(level));
    json_object_insert(j, "nodeid", json_number(nodeid));
    json_object_insert(j, "session", json_number(session));
    json_object_insert(j, "receiver", json_string(oam->name));

    const char *stream = "<unknown>";
    struct JsonValue *jstream = json_object_get_string(j, "stream");
    if (stream != NULL) {
        stream = jstream->v.string;
    }
    // we know that header 0 contains the label in the first 20 bit
    uint32_t *label = (uint32_t *) (p->buf + p->headers[0].start);
    json_object_insert(j, "label", json_number((ntohl(*label) >> 12) & 0xFFFFF));

    struct JsonValue *jlist = json_array();
    struct AddstartState st = {jlist, oam};
    hashmap_foreach(mep_starts, addstart_cb, &st);
    json_object_insert(j, "list", jlist);

    unsigned msg_len=0;
    char *j_msg = json_serialize(j, &msg_len);

    log_packet("%s:%d seq %d lvl %d T (to %s %d) - %s", stream, session, seq, level,
               reply_address, port, j_msg);

    oam_send_reply(reply_address, port, j_msg, msg_len);
    free(reply_address);
    free(j_msg);

    json_delete(j);
    return true;
}

bool oam_recv_request(struct OamEndPoint *oam, struct Packet *p)
{
    // note: we made sure in conf_actions.c that at this point of the pipeline the packet starts with mpls+dcw

    for (unsigned i=1; i<p->header_count-1; i++) {
        if (p->headers[i+1].start != p->headers[i].start + p->headers[i].len) {
            fprintf(stderr, "OAM packet is not continuous in memory at header %u type %s\n",
                    i, protocol_type_from_id(p->headers[i].type));
            return false;
        }
    }

    // let's reinterpret the header structure
    p->headers[1].type = PROTO_ID_OAM;
    p->headers[1].len = 8;
    p->headers[2].type = PROTO_ID_PAYLOAD;
    p->headers[2].start = p->headers[1].start + 8;
    p->headers[2].len = p->len - 4 - 8;
    p->header_count = 3;

    unsigned char *oam_hdr = p->buf + p->headers[1].start;
    //unsigned char seq = oam_hdr[1];
    //unsigned short channel = (oam_hdr[2]<<8)+oam_hdr[3];
    //unsigned short nodeid = (oam_hdr[4]<<8)+oam_hdr[5];
    unsigned char level = oam_hdr[6] >> 1;
    //unsigned char session = oam_hdr[7] & 0x0f;
    char *msg = (char *)(p->buf + p->headers[2].start);

    //log_packet("packet (%s) at [%s level %d], ttl %d nib_ver %x sequence %x channel %x node %x level %x session %x\njson: %s",
    //        protocol_type_from_id(p->headers[1].type), oam->name, oam->level, p->ttl, oam_hdr[0], seq, channel, nodeid, level, session, msg);

    struct JsonValue *j = json_parse(msg, strlen(msg));
    if (j==NULL || j->type != JSON_OBJECT) {
        fprintf(stderr, "Invalid JSON string in incoming OAM packet\n");
        return false;
    }

    struct JsonValue *jreqt = json_object_get_string(j, "request");
    if(jreqt==NULL) {
        fprintf(stderr, "OAM packet has no request type\n");
        json_delete(j);
        return false;
    }

    if(level < oam->level){
        /*fprintf(stderr, "MIP %s level %d Warning: dropping lower level (level %d) OAM packet.\n",
          oam->name, oam->level, level);*/
        json_delete(j);
        return false;
    }
    if(level > oam->level) {
        json_delete(j);
        return true;
    }

    struct JsonValue *target = json_object_get_string(j, "target");
    if (target == NULL) {
        json_delete(j);
        return false;
    }

    if (strcmp(jreqt->v.string, "ping") == 0) {
        struct JsonValue *jrr = json_object_get_array(j, "rr");
        if (jrr != NULL) {
            //TODO supply more info: type, level, attached object
            json_array_unshift(jrr, json_string(oam->name));

            unsigned js_length;
            char *js_string = json_serialize(j, &js_length);
            if (js_string == NULL) {
                fprintf(stderr, "could not add entry to route record\n");
                json_delete(j);
                return false;            //  DROP packet
            }
            memcpy(msg, js_string, js_length);
            free(js_string);
            p->len += js_length - p->headers[2].len;
            p->headers[2].len = js_length;
        }

        if (p->ttl == 0) {
            process_ping_request(oam, p, j);
            return false;
        }
        if (strcmp(target->v.string, oam->name) == 0) {
            process_ping_request(oam, p, j);
            return false;
        }
        if (strcmp(target->v.string, "any") == 0) {
            if (!process_ping_request(oam, p, j))
                return false;
            return oam->stop ? false : true;
        }
        return oam->stop ? false : true;
    } else if (strcmp(jreqt->v.string, "rping") == 0) {
        if (p->ttl == 0) {
            return false;
        }
        if (strcmp(target->v.string, "any") == 0) {
            return false;
        }
        if (strcmp(target->v.string, oam->name) == 0) {
            process_rping_request(oam, p, j);
            return false;
        }
        return oam->stop ? false : true;
    } else if (strcmp(jreqt->v.string, "rlist") == 0) {
        if (p->ttl == 0) {
            return false;
        }
        if (strcmp(target->v.string, "any") == 0) {
            if (!process_rlist_request(oam, p, j))
                return false;
            return oam->stop ? false : true;
        }
        if (strcmp(target->v.string, oam->name) == 0) {
            process_rlist_request(oam, p, j);
            return false;
        }
        return oam->stop ? false : true;
    } else {
        //TODO unknown message type
        return false;
    }
}

static int oam_start_background_ping_cb(const char *key, void *value, void *userdata)
{
    (void) userdata;
    const char *request = value;
    log_debug("starting OAM background ping command '%s'", key);

    if (strncmp(request, "ping", 4) != 0) {
        log_error("OAM background command '%s' is not ping", key);
        return 0;
    }
    struct oam_request *ping_req = parse_ping_command(request+4, true, false, stderr);

    if (ping_req == NULL) {
        log_error("OAM background ping command '%s' invalid", key);
        return 0;
    }

    ping_req->count = 0;    // force infinite count

    int live_session_count = 0;
    struct StreamSessions *stream = hashmap_find(session_ids, ping_req->mep_start->stream_name);
    if(stream){
        for (int i=0; i<16; i++) if (stream->sessions[i].live) live_session_count++;
        if (live_session_count >= 14) {
            free(ping_req);
            return 0;
        }
    }
    return initiate_request(ping_req);
}

bool init_oam(struct HashMap *config_oam)
{
    printf("Init OAM fuctionality.\n");

    pthread_mutex_init(&session_lock, NULL);
    session_ids = new_hashmap(11, NULL, NULL);

    // Start OAM background streams
    if (!hashmap_foreach(config_oam, oam_start_background_ping_cb, NULL)) {
        fprintf(stderr, "failed to start oam command\n");
        return NULL;
    }
    return true;
}

void finish_oam(void)
{
    struct ListParams params = {NULL};
    hashmap_foreach(session_ids, close_sessions_cb, &params);
}

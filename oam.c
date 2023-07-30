// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.

#include "oam.h"
#include "action.h"
#include "pipeline.h"
#include "configfile.h"
#include "hashmap.h"
#include "if_oam.h"
#include "if_oam_cmd.h"
#include "interface.h"
#include "packet.h"
#include "protocol.h"
#include "utils.h"
#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h> /* struct ifreq */
#include <arpa/inet.h> /* ntohs() */
#include <ifaddrs.h>
#include <sys/types.h>
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

static int nr_oam_ifaces = 0;
static struct Interface *oam_ifaces[16];
static struct Interface *oam_default_iface = NULL;
static struct Interface *oam_cmd_iface = NULL;
static struct HashMap *mep_starts = NULL; // name -> struct MEPStart

void set_oam_cmd_if(struct Interface *iface)
{
    if (oam_cmd_iface == NULL)
        oam_cmd_iface = iface;
    else
        fprintf(stderr, "only one OAM command interface is supported, config has '%s' and '%s'\n",
                oam_cmd_iface->name, iface->name);
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

static struct SessionTracker {
    time_t alloc_time;
    bool live;
} live_sessions[16] = {0};
static pthread_mutex_t session_lock;
static int alloc_session_id(void)
{
    static unsigned last_session = 0;

    pthread_mutex_lock(&session_lock);
    unsigned next_id = (last_session + 1) % 16;
    unsigned id = next_id;
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

    if (live_sessions[id].live) {
        if (now.tv_sec > live_sessions[id].alloc_time + 2) {
            //fprintf(stderr, "session %u timeouted\n", id);
            live_sessions[id].live = false;
        } else {
            id = (id + 1) % 16;
            while (live_sessions[id].live && id != next_id) {
                if (now.tv_sec > live_sessions[id].alloc_time + 2) {
                    //fprintf(stderr, "session %u timeouted\n", id);
                    live_sessions[id].live = false;
                    break;
                }
                id = (id + 1) % 16;
            }
        }
    }

    if (live_sessions[id].live) {
        pthread_mutex_unlock(&session_lock);
        return -1;
    } else {
        last_session = id;
        live_sessions[id].live = true;
        live_sessions[id].alloc_time = now.tv_sec + 1;
        pthread_mutex_unlock(&session_lock);
        return id;
    }
}

static void session_finished(unsigned id)
{
    if (id >= 16) {
        fprintf(stderr, "invalid session finish id %u\n", id);
        return;
    }
    if (live_sessions[id].live == false) {
        fprintf(stderr, "session finish id %u points to inactive session\n", id);
        return;
    }
    live_sessions[id].live = false;
}

unsigned short get_oam_nodeid(void)
{
    return oam_get_uid(oam_default_iface);
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

static int oam_send_request(FILE *cmd_w, const char *type, struct Interface *iface, unsigned session_id, unsigned seq, const char *mep_start, const char *mep_stop, int level, unsigned char ttl, int rr, int os)
{
    struct MepStart *mep = hashmap_find(mep_starts, mep_start);
    if (!mep) {
        fprintf(cmd_w, "invalid mep start name '%s'\n", mep_start);
        return -EINVAL;
    }
    struct Pipeline *pipe = mep->pipe;
    if (!pipe) {
        fprintf(cmd_w, "mep start '%s' has no pipeline!?!\n", mep_start);
        return -EINVAL;
    }

    struct Packet *packet = new_packet(NULL);
    const char *return_ip = oam_get_ip(iface);
    unsigned return_port = oam_get_port(iface);
    unsigned short node_id = oam_get_uid(iface);

    fprintf(cmd_w, "OAM packet %s session %u seq %u, %s -> %s, level %d, rr: %s os: %s\t[reply to ip: %s, port: %u]\n",
            type, session_id, seq, mep_start, mep_stop, level, rr?"yes":"no", os?"yes":"no",
            return_ip, return_port);

    add_fixed_headers(packet, ttl, seq, OAM_CHANNEL,
            node_id, level, session_id);
    packet->ttl = ttl;

    struct JsonValue *js = json_object();
    json_object_insert(js, "request", json_string(type));
    json_object_insert(js, "target", json_string(mep_stop));
    //TODO target node id
    json_object_insert(js, "level", json_number(level));
    struct timespec sendtime;
    clock_gettime(CLOCK_REALTIME, &sendtime);
    json_object_insert(js, "send_s", json_number(sendtime.tv_sec));
    json_object_insert(js, "send_ns", json_number(sendtime.tv_nsec));
    struct JsonValue *jret = json_object();
    json_object_insert(jret, "ip", json_string(return_ip));
    json_object_insert(jret, "port", json_number(return_port));
    json_object_insert(js, "return", jret);

    if(rr == 1){
        jret = json_object();
        json_object_insert(jret, "0", json_string(mep_start));
        json_object_insert(js, "rr", jret);
    }
    if(os == 1){
        jret = json_object();
        json_object_insert(js, "objects", jret);
    }

    unsigned js_length;
    char *js_string = json_serialize(js, &js_length);
    json_delete(js);
    if (js_string == NULL) {
        //TODO can this happen?
        delete_packet(packet);
        return -ENOMSG;
    }
    packet_add_header(packet, 2, PROTO_ID_PAYLOAD, js_length);
    unsigned char *msg = packet->buf + packet->headers[2].start;
    memcpy(msg, js_string, js_length);
    free(js_string);

    struct PipelineIterator *pi = new_pipe_iterator(pipe, packet);
    pi->pos = mep->pipe_pos_idx;

    pipe_iterator_run(pi);
    return 0;
}

pthread_t ping_tid;
struct oam_request{                 // needed for the ping thread. Shuld be used in other function calls too
    FILE *cmd_w;
    struct Interface *iface;
    unsigned session_id, seq;
    const char *mep_start, *mep_stop;
    int level, rr, os;
    unsigned count;
  };

static void *oam_ping_thread(void *arg)
{
    struct oam_request *req = (struct oam_request *)arg;
    printf("Sending %d packets\n", req->count);
    for(unsigned seq=0; seq<req->count; seq++){
        oam_send_request(req->cmd_w, "ping", req->iface, req->session_id, seq, req->mep_start, req->mep_stop, req->level, OAM_PING_TTL, req->rr, req->os);
        sleep(1);
    }
    return NULL;
}

static int oam_ping(FILE *cmd_w, struct Interface *iface, unsigned session_id, unsigned seq, const char *mep_start, const char *mep_stop, int level, int rr, int os, unsigned count)
{
    if(count == 1)
        return oam_send_request(cmd_w, "ping", iface, session_id, seq, mep_start, mep_stop, level, OAM_PING_TTL, rr, os);
    else{
          struct oam_request ping_req;
          ping_req.cmd_w = cmd_w;
          ping_req.iface = iface;
          ping_req.session_id = session_id;
          ping_req.seq = seq;
          ping_req.mep_start = mep_start;
          ping_req.mep_stop = mep_stop;
          ping_req.level = level;
          ping_req.rr = rr;
          ping_req.os = os;
          ping_req.count = count;

          pthread_attr_t attr;
          if ((errno = pthread_attr_init(&attr)) != 0) {
              perror("oam ping thread pthread_attr_init");
              return -1;
          }

          if (pthread_create(&ping_tid, &attr, &oam_ping_thread, &ping_req) != 0) {
              fprintf(stderr, "could not create new ping thread\n");
              return -1;
          }
    }

    return 0;
}

static const char help_str[] =
    "Available commands:\n"
    "help - get help\n"
    "exit - exit OAM\n"
    "list - list monitoring start points\n"
    "ping[@if] <stream:mep-start> <mep-stop/mip/any> <level> [-r] [-n <count>]\n";

struct ListMepParams {
    FILE *cmd_w;
};
static int list_mep_cb(const char *key, void *value, void *userdata)
{
    struct MepStart *start = value;
    struct ListMepParams *params = userdata;
    fprintf(params->cmd_w, "%s level %d\n", key, start->level);
    return 1;
}

int oam_command_loop(int cmd_fd)
{
#define ERROR(msg, ...)                             \
    fprintf(cmd_w, "Error: " msg "\n",              \
        ##__VA_ARGS__);                             \
    continue

    // inverse operation: fd=fileno(file)
    FILE *cmd_w = fdopen(cmd_fd, "w");
    setvbuf(cmd_w, NULL, _IOLBF, 0);
    //TODO if we wand to fread() we need to duplicate the handle
    //int cmd_fd_dup = dup(cmd_fd);
    //FILE *cmd_r = fdopen(cmd_fd_dup, "r");

    char oam_command[255];
    char mep_start[32], mep_stop[32], ifname[32], opts[32], c;
    int level, rr=0, count=1, os=0;
    int n, k, val;
    struct Interface *oam_if;

    fprintf(cmd_w, "OAM ready.\n");

    while (true) {
        n = read(cmd_fd, oam_command, sizeof(oam_command)-1);
        if (n > 0) {
            oam_command[n] = 0;
            //printf("oam command '%s' length %d\n", oam_command, n);
            if(strcmp(oam_command, "exit\r\n") == 0){
                fprintf(cmd_w, "Exiting.\n");
                break;
            }
            else if(strcmp(oam_command, "help\r\n") == 0){
                fprintf(cmd_w, help_str);
            }
            else if (strcmp(oam_command, "list\r\n") == 0) {
                fprintf(cmd_w, "Available MEP Start points:\n");
                struct ListMepParams params = {cmd_w};
                hashmap_foreach_sorted(mep_starts, list_mep_cb, &params);
            }
            else if(strncmp(oam_command, "ping",4) == 0){
                if(oam_command[4]=='@'){
                    k = sscanf(oam_command, "ping@%s %s %s %d %[^\n]",
                            ifname, mep_start, mep_stop, &level, opts);
                    if (k < 4) {
                        ERROR("ping arguments invalid");
                    }
                    oam_if = get_oam_if(ifname);
                    if (oam_if == NULL) {
                        ERROR("invalid interface name: %s", ifname);
                    }
                }else{
                    k = sscanf(oam_command, "ping %s %s %d %[^\n]",
                            mep_start, mep_stop, &level, opts);
                    if (k < 3) {
                        ERROR("ping arguments invalid");
                    }
                    oam_if = oam_default_iface;
                }

                // process options
                char *po = opts;
                while(*po == ' ') po++; // skip spaces
                while(sscanf(po, "-%c %d", &c, &val) != 0){
                    if(c=='r'){
                        while(*po != ' ') po++; // skip -r
                        rr = 1;
                    } else if(c=='o'){
                      while(*po != ' ') po++; // skip -r
                      os = 1;
                    } else if(c=='n'){
                        count = val;
                        while(*po != ' ') po++; // skip -n
                        while(*po == ' ') po++; // skip any extra spaces
                        while(*po != ' ') po++; // skip <count>
                    } else{
                      printf("unknown option %c \n", c);
                      while(*po != ' ') po++;
                    }
                    while(*po == ' ') po++; // skip spaces
                }

                int session_id = alloc_session_id();
                unsigned seq = 0;
                if (session_id >= 0) {
                    fprintf(cmd_w, "OK %d, ping @[%s] %s -> %s, level %d\n",
                            seq, oam_if->name, mep_start, mep_stop, level);

                    if (oam_ping(cmd_w, oam_if, session_id, seq, mep_start, mep_stop, level, rr, os, count) != 0) {
                        ERROR("can't send ping");
                    }
                } else {
                    ERROR("too many ongoing OAM sessions");
                }
            }
        }
        else break;
    }
    return 0;
}



/*
 * Handle received UDP OAM reply mesage
 * Msg: pointer to the message
 * Return 0 on success
*/
int oam_recv_reply(char *msg)
{
    char reply_str[512];
    struct JsonValue *j = json_parse(msg, strlen(msg));
    struct JsonValue *nid = hashmap_find(j->v.object, "nodeid");
    if(nid==NULL) {
        fprintf(stderr, "No nodeid in reply.\n");
        return -1;
    }
    struct JsonValue *sess = hashmap_find(j->v.object, "session");
    if(sess == NULL) {
        fprintf(stderr, "No session id in reply.\n");
        return -1;
    } else {
        if (sess->type != JSON_NUMBER) {
            fprintf(stderr, "session id in reply is not number\n");
        } else
        if (sess->v.number < 0 || sess->v.number > 15) {
            fprintf(stderr, "session id %.0f in reply is invalid\n", sess->v.number);
        } else
            session_finished(sess->v.number);
    }
    struct JsonValue *request = hashmap_find(j->v.object, "request");
    if(request == NULL) {
        fprintf(stderr, "No request in reply.\n");
        return -1;
    }
    struct JsonValue *target = hashmap_find(j->v.object, "target");
    if(target == NULL) {
        fprintf(stderr, "No target in reply.\n");
        return -1;
    }
    struct JsonValue *seq = hashmap_find(j->v.object, "sequence");
    if(seq == NULL) {
        fprintf(stderr, "No sequence in reply.\n");
        return -1;
    }
    struct JsonValue *level = hashmap_find(j->v.object, "level");
    if(level == NULL) {
        fprintf(stderr, "No level in reply.\n");
        return -1;
    }
    struct JsonValue *node = hashmap_find(j->v.object, "node");
    if(node == NULL) {
        fprintf(stderr, "No node in reply.\n");
        return -1;
    }
    sprintf(reply_str,"[session %.0f nodeid %.0f]\t%s level %.0f target %s seq %.0f\treply from %s", sess->v.number, nid->v.number,
            request->v.string, level->v.number, target->v.string, seq->v.number, node->v.string);

    struct JsonValue *jrr = hashmap_find(j->v.object, "rr");
    if(jrr){
        strcat(reply_str, "\troute: ");
        for(unsigned i=0; i<hashmap_count(jrr->v.object); i++){
            char hop[32];
            sprintf(hop, "%d",i);
            struct JsonValue *route = hashmap_find(jrr->v.object, hop);
            strcat(reply_str, route->v.string);
            strcat(reply_str, " ");
        }
    }

    struct JsonValue *jos = hashmap_find(j->v.object, "objects");
    if(jos){
        // ToDo: formatted printout per object type
        strcat(reply_str, " os: ");
        unsigned jos_length;
        char *jos_string = json_serialize(jos, &jos_length);
        strcat(reply_str, jos_string);
        free(jos_string);
      }

    strcat(reply_str, "\n");
    json_delete(j);

    if(oam_cmd_iface != NULL)
        return oam_cmd_recv_reply(oam_cmd_iface, reply_str);
    else
        return -1;
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


// Initialize OAM functionality
bool init_oam(struct R2d2Config *config)
{
    printf("Init OAM fuctionality.\n");
    (void)config;
    //TODO what should this function do?

    pthread_mutex_init(&session_lock, NULL);

    return true;
}

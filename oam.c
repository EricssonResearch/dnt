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
#include "seq_gen.h"
#include "seq_recov.h"
#include "utils.h"
#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
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
    struct SequenceGenerator *seqgen; //TODO do we need this?
    int level;
};

static int nr_oam_ifaces = 0;
static struct Interface *oam_ifaces[16];
static struct Interface *oam_default_iface = NULL;
static struct Interface *oam_cmd_iface = NULL;
static struct HashMap *mep_starts = NULL; // name -> struct MEPStart

// TODO: make struct OamSession if more per-session info needed for MEP/MIP.
// currently the only state of the session is the seq recovery
static struct HashMap *oam_seq_recoveries = NULL; // session_id -> struct SequenceRecovery

static unsigned session_counter = 0;
static unsigned seq_counter = 0; // according to the RFC draft this is node-global

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

struct SequenceRecovery *get_oam_rcvy(char *key)
{
    if (oam_seq_recoveries == NULL)
        oam_seq_recoveries = new_hashmap(51, NULL, NULL);
    struct SequenceRecovery *rec = hashmap_find(oam_seq_recoveries, key);
    if (rec == NULL) {
        rec = new_seq_rec(RCVY_Match, false, false, 0, OAM_RCVY_RESET_MS, 0, key);
        hashmap_insert(oam_seq_recoveries, key, rec);
    }
    return rec;
}

void delete_oam_rcvy(char *key)
{
    struct SequenceRecovery *rec = hashmap_find(oam_seq_recoveries, key);
    if (rec) {
        hashmap_remove(oam_seq_recoveries, key);
        delete_seq_rec(rec);
    }
}

int oam_create_mep_start(const char *stream_name, const char *mep_name, int level, unsigned idx)
{
    if (mep_starts == NULL) {
        mep_starts = new_hashmap(29, NULL, NULL);
    }
    struct MepStart *mepstart = hashmap_find(mep_starts, mep_name);
    if (mepstart) {
        fprintf(stderr, "MEP Start '%s' defined twice, in streams '%s' and '%s'\n",
                mep_name, mepstart->stream_name, stream_name);
        return -1;
    }
    mepstart = calloc_struct(MepStart);
    mepstart->name = strdup(mep_name);
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

static int oam_send_request(const char *type, struct Interface *iface, unsigned session_id, unsigned seq, char *stream, char *mep_start, char *mep_stop, int level, unsigned char ttl)
{
    struct MepStart *mep = hashmap_find(mep_starts, mep_start);
    if (!mep)
        return -EINVAL;
    struct Pipeline *pipe = mep->pipe;
    if (!pipe)
        return -EINVAL;

    struct Packet *packet = new_packet(NULL);
    const char *return_ip = oam_get_ip(iface);
    unsigned return_port = oam_get_port(iface);
    unsigned short node_id = oam_get_uid(iface);

    printf("OAM %s session %u seq %u, %s : %s -> %s, level %d\n resp ip: %s, port: %u\n", type,
            session_id, seq, stream, mep_start, mep_stop, level,
            return_ip, return_port);

    add_fixed_headers(packet, ttl, seq, OAM_CHANNEL,
            node_id, level, session_id);

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

static int oam_ping(struct Interface *iface, unsigned session_id, unsigned seq, char *stream, char *mep_start, char *mep_stop, int level)
{
    return oam_send_request("ping", iface, session_id, seq, stream, mep_start, mep_stop, level, OAM_PING_TTL);
}

static int oam_trace(struct Interface *iface, unsigned session_id, unsigned seq, char *stream, char *mep_start, char *mep_stop, int level)
{
    return oam_send_request("trace", iface, session_id, seq, stream, mep_start, mep_stop, level, 1);
}

static int oam_discovery(struct Interface *iface, unsigned session_id, unsigned seq, char *stream, char *mep_start, char *mep_stop, int level)
{
    return oam_send_request("discovery", iface, session_id, seq, stream, mep_start, mep_stop, level, OAM_PING_TTL);
}

static const char welcome_str[] = "OAM ready.\n";

static const char help_str[] =
    "Available commands:\n"
    "help - get help\n"
    "exit - exit OAM\n"
    "ping[@if] <stream:mep-start> <mep-stop/mip/any> <level>\n"
    "trace[@if] <stream:mep-start> <mep-stop/mip> <level>\n"
    "discovery[@if] <stream:mep-start> <mep-stop/mip> <level>\n";

int oam_command_loop(int cmd_fd)
{
#define ERROR(msg, ...)                             \
    sprintf(resp, "Error: " msg "\n",               \
##__VA_ARGS__);                             \
    if (send(cmd_fd, resp, sizeof(resp), 0) == -1)  \
    perror("send");                                 \
    continue

    char oam_command[255];
    char stream[32],mep_start[32], mep_stop[32], ifname[32];
    char resp[255];
    int level;
    int n, k;
    struct Interface *oam_if;

    // each OAM command connection is an unique session
    unsigned session_id = __atomic_fetch_add(&session_counter, 1, __ATOMIC_RELAXED);

    if (send(cmd_fd, welcome_str, sizeof(welcome_str), 0) == -1)
        perror("send");

    while (true) {
        n = read(cmd_fd, oam_command, sizeof(oam_command)-1);
        if (n > 0) {
            oam_command[n] = 0;
            if(strcmp(oam_command, "exit") == 0){
                if (send(cmd_fd, "Exiting.\n", 9, 0) == -1)
                    perror("send");
                break;
            }
            else if(strcmp(oam_command, "help") == 0){
                if (send(cmd_fd, help_str, sizeof(help_str), 0) == -1)
                    perror("send");
            }
            else if(strncmp(oam_command, "ping",4) == 0){
                if(oam_command[4]=='@'){
                    k = sscanf(oam_command, "ping@%s %[^:]:%s %s %d",
                            ifname, stream, mep_start, mep_stop, &level);
                    if (k != 5) {
                        ERROR("ping arguments invalid");
                    }
                    oam_if = get_oam_if(ifname);
                    if (oam_if == NULL) {
                        ERROR("invalid interface name: %s", ifname);
                    }
                }else{
                    k = sscanf(oam_command, "ping %[^:]:%s %s %d",
                            stream, mep_start, mep_stop, &level);
                    if (k != 4) {
                        ERROR("ping arguments invalid");
                    }
                    oam_if = oam_default_iface;
                }

                unsigned seq = __atomic_fetch_add(&seq_counter, 1, __ATOMIC_RELAXED);
                sprintf(resp, "OK %d, ping @[%s] %s : %s -> %s, level %d\n",
                        seq, oam_if->name, stream, mep_start, mep_stop, level);
                if (send(cmd_fd, resp, strlen(resp), 0) == -1)
                    perror("send");

                if (oam_ping(oam_if, session_id, seq, stream, mep_start, mep_stop, level) != 0) {
                    ERROR("can't send ping");
                }
            }
            else if(strncmp(oam_command, "trace",5) == 0){
                if(oam_command[5]=='@'){
                    k = sscanf(oam_command, "trace@%s %[^:]:%s %s %d",
                            ifname, stream, mep_start, mep_stop, &level);
                    if (k != 5) {
                        ERROR("trace arguments invalid");
                    }
                    oam_if = get_oam_if(ifname);
                    if (oam_if == NULL) {
                        ERROR("invalid interface name: %s", ifname);
                    }
                }else{
                    k = sscanf(oam_command, "trace %[^:]:%s %s %d", stream, mep_start, mep_stop, &level);
                    if (k != 4) {
                        ERROR("trace arguments invalid");
                    }
                    oam_if = oam_default_iface;
                }

                unsigned seq = __atomic_fetch_add(&seq_counter, 1, __ATOMIC_RELAXED);
                sprintf(resp, "OK %d, trace @[%s] %s : %s -> %s, level %d\n",
                        seq, oam_if->name, stream, mep_start, mep_stop, level);
                if (send(cmd_fd, resp, strlen(resp), 0) == -1)
                    perror("send");

                if (oam_trace(oam_if, session_id, seq, stream, mep_start, mep_stop, level) != 0) {
                    ERROR("can't send trace");
                }
            }
            else if(strncmp(oam_command, "discovery",9) == 0){
                if(oam_command[9]=='@'){
                    k = sscanf(oam_command, "discovery@%s %[^:]:%s %s %d",
                            ifname, stream, mep_start, mep_stop, &level);
                    if (k != 5) {
                        ERROR("discovery arguments invalid");
                    }
                    oam_if = get_oam_if(ifname);
                    if (oam_if == NULL) {
                        ERROR("invalid interface name: %s", ifname);
                    }
                }else{
                    sscanf(oam_command, "discovery %[^:]:%s %s %d", stream, mep_start, mep_stop, &level);
                    oam_if = oam_default_iface;
                }

                unsigned seq = __atomic_fetch_add(&seq_counter, 1, __ATOMIC_RELAXED);
                k = sprintf(resp, "OK %d, discovery @[%s] %s : %s -> %s, level %d\n",
                        seq, oam_if->name, stream, mep_start, mep_stop, level);
                if (k != 4) {
                    ERROR("discovery arguments invalid");
                }
                if (send(cmd_fd, resp, strlen(resp), 0) == -1)
                    perror("send");

                if (oam_discovery(oam_if, session_id, seq, stream, mep_start, mep_stop, level) != 0) {
                    ERROR("can't send discovery");
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
  struct JsonValue *j = json_parse(msg, strlen(msg));
  struct JsonValue *val = hashmap_find(j->v.object, "seq_id");
  if(val!=NULL)
      printf("seq_id: %.0f\n", val->v.number);
  val = hashmap_find(j->v.object, "type");
  if(val!=NULL)
      printf("msg type: %s\n", val->v.string);
  json_delete(j);

  if(oam_cmd_iface != NULL)
    return oam_cmd_recv_reply(oam_cmd_iface, msg);
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

    return true;
}

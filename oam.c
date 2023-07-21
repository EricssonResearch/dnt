// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.

#include "oam.h"
#include "action.h"
#include "pipeline.h"
#include "conf_interface.h"
#include "conf_streams.h"
#include "configfile.h"
#include "hashmap.h"
#include "if_oam.h"
#include "if_oam_cmd.h"
#include "if_utils.h"
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
    struct SequenceGenerator *seqgen;
    int level;
};

int nr_oam_ifaces = 0;
struct Interface *oam_ifaces[16];
struct Interface *oam_cmd_iface = NULL;
struct HashMap *mep_starts = NULL; // name -> struct MEPStart

// TODO: make struct OamSession if more per-session info needed for MEP/MIP.
// currently the only state of the session is the seq recovery
struct HashMap *oam_seq_recoveries = NULL; // session_id -> struct SequenceRecovery

unsigned cmd_id = 1000;

void set_oam_cmd_if(struct Interface *iface)
{
    oam_cmd_iface = iface;
}

void add_oam_if(struct Interface *iface)
{
    if (nr_oam_ifaces < 16) {
        oam_ifaces[nr_oam_ifaces] = iface;
        nr_oam_ifaces++;
    }
}

struct Interface *get_oam_cmd_if(const char *name)
{
    (void) name;
    return oam_cmd_iface;
}

struct Interface *get_oam_if(const char *name)
{
    for (int i = 0; i < nr_oam_ifaces; ++i) {
        if (strcmp(name, oam_ifaces[i]->name) == 0) {
            return oam_ifaces[i];
        }
    }
    return NULL;
}

struct SequenceRecovery *get_oam_rcvy(char *session_id)
{
    if (oam_seq_recoveries == NULL)
        oam_seq_recoveries = new_hashmap(51, NULL, NULL);
    struct SequenceRecovery *rec = hashmap_find(oam_seq_recoveries, session_id);
    if (rec == NULL) {
        rec = new_seq_rec(RCVY_Match, false, false, 0, OAM_RCVY_RESET_MS, 0, session_id);
        hashmap_insert(oam_seq_recoveries, session_id, rec);
    }
    return rec;
}

void delete_oam_rcvy(char *session_id)
{
    struct SequenceRecovery *rec = hashmap_find(oam_seq_recoveries, session_id);
    if (rec) {
        hashmap_remove(oam_seq_recoveries, session_id);
        delete_seq_rec(rec);
    }
}

void oam_create_mep_start(const char *stream_name, const char *mep_name, int level, unsigned idx)
{
    if (mep_starts == NULL) {
        mep_starts = new_hashmap(29, NULL, NULL);
    }
    struct MepStart *mepstart = calloc_struct(MepStart);
    mepstart->name = strdup(mep_name);
    mepstart->stream_name = strdup(stream_name);
    mepstart->level = level;
    mepstart->pipe_pos_idx = idx;
    // for mepstart->pipe see oam_set_pipeline_for_mep_start()
    hashmap_insert(mep_starts, mepstart->name, mepstart);
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


static int oam_ping(struct Interface *iface, unsigned id, char *stream, char *mep_start, char *mep_stop, int level)
{
    printf("OAM ping id %d, from %s : %s -> %s, level %d\n", id, stream, mep_start, mep_stop, level);
    printf("OAM resp ip: %s, port: %u\n", oam_get_oam_ip(iface), oam_get_oam_port(iface));

    struct MepStart *mep = hashmap_find(mep_starts, mep_start);
    if (!mep)
        return -EINVAL;
    struct Pipeline *pipe = mep->pipe;
    if (!pipe)
        return -EINVAL;

    struct Packet *packet = new_packet(NULL);
    const char *return_ip = oam_get_oam_ip(iface);
    unsigned return_port = oam_get_oam_port(iface);
    add_fixed_headers(packet, OAM_PING_TTL, 0, OAM_CHANNEL,
            42, //TODO nodeid is the lower 2 octets of the return address
            level, id);

    struct JsonValue *js = json_object();
    json_object_insert(js, "command", json_string("ping"));
    json_object_insert(js, "target", json_string(mep_stop));
    //TODO target node id
    json_object_insert(js, "level", json_number(level));
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


/* int oam_ping(unsigned id, char *stream, char *mep_start, char *mep_stop, int level){ */
/*   printf("OAM ping id %d, from %s : %s -> %s, level %d\n", id, stream, mep_start, mep_stop, level); */
/**/
  /* TODO : remove, just for  testing ->  */
/**/
/*   char msg[]="This is an OAM reply test message.\n\0"; */
/**/
  // get OAM dest IP */
/*   char addr[INET_ADDRSTRLEN]; */
/*   struct sockaddr_in saddr; */
/*   struct Value ip = {&saddr.sin_addr, 0, 32}; */
/*   if(nr_oam_ifaces > 0){ */
/*     value_producer *read = oam_ifaces[0]->get_property_reader(oam_ifaces[0], "ip", FT_IPV4ADDRESS, &ip); */
/*     if (read == NULL) { */
/*       printf("interface %s has no property named 'ip'", oam_ifaces[0]->name); */
/*     } */
/*     unsigned int act_idx; */
/*     struct Action *a = find_mep_start(oid->config, stream, mep_start, &act_idx); */
/*     if (!a) { */
/*         return -EINVAL; */
/*     } */
/*     // Using OAM CMD interface as egress, allocating the OAM packet here */
/*     // The MEP Start action act on source interface type */
/*     struct Packet *packet = new_packet(if_oam_cmd); */
/*     unsigned int proto_id = PROTO_ID_MPLS; */
/*     packet_add_header(packet, 0, proto_id, protocol_list[proto_id].bytelength); */
/*     proto_id = PROTO_ID_OAM; */
/*     packet_add_header(packet, 0, proto_id, protocol_list[proto_id].bytelength); */
/*     struct PipelineIterator *pi = new_pipe_iterator(pipe, packet); */
/**/
/*   oam_send_reply(addr, msg); */
  /*   <- testing   */
/**/
/**/
/*   return 0; */
/* } */

static int oam_trace(struct Interface *iface, unsigned id, char *stream, char *mep_start, char *mep_stop, int level)
{
  printf("OAM trace id %d, from %s : %s -> %s, level %d\n", id, stream, mep_start, mep_stop, level);

  char msg[]="{ \"seq_id\": 1002, \"type\": \"mip\", \"name\": \"mip02\", \"message\": \"ping\", \"object\": { \"type\": \"replicate\", \"name\": \"prf3\", \"passed\": 34 } }\n";
  oam_send_reply(oam_get_oam_ip(iface), oam_get_oam_port(iface), msg);

  return 0;
}

static int oam_discovery(struct Interface *iface, unsigned id, char *stream, char *mep_start, char *mep_stop, int level)
{
  printf("OAM discovery id %d, from %s : %s -> %s, level %d\n", id, stream, mep_start, mep_stop, level);
  printf("OAM resp ip: %s, port: %u\n", oam_get_oam_ip(iface), oam_get_oam_port(iface));

  return 0;
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
    char oam_command[255];
    char stream[32],mep_start[32], mep_stop[32], ifname[32];
    char resp[255];
    int level;
    int n;
    struct Interface *oam_if;

    if (send(cmd_fd, welcome_str, sizeof(welcome_str), 0) == -1)
        perror("send");

    ifname[0]=0;  // init to empty string

    while (true) {
        n = read(cmd_fd, oam_command, sizeof(oam_command));
        if (n > 0) {
            if(strncmp(oam_command, "exit",4) == 0){
                if (send(cmd_fd, "Exiting.\n", 9, 0) == -1)
                    perror("send");
                break;
            }
            if(strncmp(oam_command, "help",4) == 0){
                if (send(cmd_fd, help_str, sizeof(help_str), 0) == -1)
                    perror("send");
            }
            if(strncmp(oam_command, "ping",4) == 0){
                cmd_id++;
                if(oam_command[4]=='@'){
                    sscanf(oam_command, "ping@%s %[^:]:%s %s %d", ifname, stream, mep_start, mep_stop, &level);
                    oam_if = get_oam_if(ifname);
                    if(oam_if==NULL){
                        sprintf(resp,"Error: invalid interface name: %s\n", ifname);
                        if (send(cmd_fd, resp, sizeof(resp), 0) == -1)
                            perror("send");
                        continue;
                    }
                    sprintf(resp, "OK %d, ping @[%s] %s : %s -> %s, level %d\n", cmd_id, ifname, stream, mep_start, mep_stop, level);
                }else{
                    sscanf(oam_command, "ping %[^:]:%s %s %d", stream, mep_start, mep_stop, &level);
                    oam_if = oam_ifaces[0];
                    sprintf(resp, "OK %d, ping %s : %s -> %s, level %d\n", cmd_id, stream, mep_start, mep_stop, level);
                }
                // acknowledge with session_id
                if (send(cmd_fd, resp, sizeof(resp), 0) == -1)
                    perror("send");
                // call the OAM ping function
                int ret = oam_ping(oam_if, cmd_id, stream, mep_start, mep_stop, level);
                if (ret < 0) {
                    sprintf(resp, "Err %d: invalid argument\n", cmd_id);
                    if (send(cmd_fd, resp, sizeof(resp), 0) == -1)
                        perror("send");
                }
            }
            if(strncmp(oam_command, "trace",5) == 0){
                cmd_id++;
                if(oam_command[5]=='@'){
                    sscanf(oam_command, "trace@%s %[^:]:%s %s %d", ifname, stream, mep_start, mep_stop, &level);
                    oam_if = get_oam_if(ifname);
                    if(oam_if==NULL){
                        sprintf(resp,"Error: invalid interface name: %s\n", ifname);
                        if (send(cmd_fd, resp, sizeof(resp), 0) == -1)
                            perror("send");
                        continue;
                    }
                    sprintf(resp, "OK %d, trace @[%s] %s : %s -> %s, level %d\n", cmd_id, ifname, stream, mep_start, mep_stop, level);
                }else{
                    sscanf(oam_command, "trace %[^:]:%s %s %d", stream, mep_start, mep_stop, &level);
                    oam_if = oam_ifaces[0];
                    sprintf(resp, "OK %d, trace %s : %s -> %s, level %d\n", cmd_id, stream, mep_start, mep_stop, level);
                }
                // acknowledge with session_id
                if (send(cmd_fd, resp, sizeof(resp), 0) == -1)
                    perror("send");
                // call the OAM trace function
                oam_trace(oam_if, cmd_id, stream, mep_start, mep_stop, level);
            }
            if(strncmp(oam_command, "discovery",9) == 0){
                cmd_id++;
                if(oam_command[9]=='@'){
                    sscanf(oam_command, "discovery@%s %[^:]:%s %s %d", ifname, stream, mep_start, mep_stop, &level);
                    oam_if = get_oam_if(ifname);
                    if(oam_if==NULL){
                        sprintf(resp,"Error: invalid interface name: %s\n", ifname);
                        if (send(cmd_fd, resp, sizeof(resp), 0) == -1)
                            perror("send");
                        continue;
                    }
                    sprintf(resp, "OK %d, discovery @[%s] %s : %s -> %s, level %d\n", cmd_id, ifname, stream, mep_start, mep_stop, level);
                }else{
                    sscanf(oam_command, "discovery %[^:]:%s %s %d", stream, mep_start, mep_stop, &level);
                    oam_if = oam_ifaces[0];
                    sprintf(resp, "OK %d, discovery %s : %s -> %s, level %d\n", cmd_id, stream, mep_start, mep_stop, level);
                }
                // acknowledge with session_id
                if (send(cmd_fd, resp, sizeof(resp), 0) == -1)
                    perror("send");
                // call the OAM discovery function
                oam_discovery(oam_if, cmd_id, stream, mep_start, mep_stop, level);
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
int oam_send_reply(const char *address, unsigned port, char *msg)
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
    if (sendto(sock, msg, strlen(msg), 0, rp->ai_addr, rp->ai_addrlen) == 0) {
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

  /*  - not needed, init from config
  unsigned port = OAM_CMD_PORT;
  unsigned ver = 4;
  if (!init_oam_cmd_interface(&config->ifaces[config->ifcount], "OAM-CMD", NULL, "0.0.0.0", port, ver)) {
      printf("failed to create oam interface");
  }

  struct Interface *cmd_iface = &config->ifaces[config->ifcount];
  config->ifcount++;

  port = OAM_PORT;
  if (!init_oam_interface(&config->ifaces[config->ifcount], "OAM", NULL, "0.0.0.0", port, ver, cmd_iface)) {
      printf("failed to create oam interface");
  }
  config->ifcount++;
  */
  return true;
}

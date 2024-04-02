// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#define OAM_INTERNAL

#include "oam_request.h"
#include "oam.h"
#include "oam_command.h"
#include "oam_core.h"
#include "oam_message.h"

#include "if_oam.h"
#include "json.h"
#include "log.h"
#include "packet.h"
#include "thread_utils.h"
#include "time_utils.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <unistd.h>
#include <arpa/inet.h>

#define OAM_PING_TTL 64
#define OAM_CHANNEL 1 /* Management Communication Channel (MCC), similar format to ours */

struct oam_request {
    char *conn_name; // NULL if not issued from a command connection
    char *return_ip;
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
    char *error;
};

DEFAULT_LOGGING_MODULE(OAM, INFO);

// @conn_name will be owned by the request
static struct oam_request *new_oam_request(const char *type, char *conn_name)
{
    struct oam_request *req = calloc_struct(oam_request);

    req->conn_name = conn_name;
    req->type = type;
    req->ttl = OAM_PING_TTL;
    req->count = 1;
    req->interval_ms = 1000;

    return req;
}

// parses @str, accepts 'ipv4' 'ipv4:port', 'ipv6', '[ipv6]', '[ipv6]:port'
// TODO accept domain names?
// allocates a new string for @ip
// @returns true on success, otherwise doesn't touch the output parameter
// TODO move this to common utils
static bool parse_ip_port(const char *str, char **ip, unsigned *port)
{
    if (str[0] == '[') {
            int n=1;
            while (str[n]) {
                if (str[n] == ']') break;
                n++;
            }
            if (str[n] == ']') {
                char *ip6_s = strndup(str+1, n-1);
                struct in6_addr ip6;
                if (inet_pton(AF_INET6, ip6_s, &ip6) != 1) {
                    free(ip6_s);
                    return false;
                }
                n++;
                if (str[n]) {
                    unsigned p;
                    char err;
                    if (sscanf(str+n, ":%u%c", &p, &err) != 1) {
                        free(ip6_s);
                        return false;
                    }
                    *port = p;
                }
                *ip = ip6_s;
                return true;
            } else {
                // missing ']'
                return false;
            }
    } else {
        struct in6_addr ip6;
        if (inet_pton(AF_INET6, str, &ip6) == 1) {
            // IPv6 without port
            *ip = strdup(str);
            return true;
        } else {
            char *colon = strchr(str, ':');
            if (colon) {
                char *colon2 = strchr(colon+1, ':');
                if (colon2) {
                    return false;
                }
                char *ip_s = strndup(str, colon-str);
                struct in_addr ip4;
                //TODO check with getaddrinfo to accept domain name?
                if (inet_pton(AF_INET, ip_s, &ip4) != 1) {
                    free(ip_s);
                    return false;
                }
                unsigned p;
                char err;
                if (sscanf(colon, ":%u%c", &p, &err) != 1) {
                    free(ip_s);
                    return false;
                }
                *ip = ip_s;
                *port = p;
                return true;
            } else {
                // no port
                char *ip_s = strdup(str);
                struct in_addr ip4;
                //TODO check with getaddrinfo to accept domain name?
                if (inet_pton(AF_INET, ip_s, &ip4) != 1) {
                    free(ip_s);
                    return false;
                }
                *ip = ip_s;
                return true;
            }
        }
        return false;
    }
}

static bool parse_ping_returnif(struct oam_request *ping_req, const char *ifname)
{
    struct Interface *iface = get_oam_interface(ifname);
    if (iface == NULL) {
        ping_req->return_port = OAM_PORT;
        if (parse_ip_port(ifname, &ping_req->return_ip, &ping_req->return_port)) {
            ping_req->node_id = get_node_id();
            log_debug("return ip '%s' port %u", ping_req->return_ip, ping_req->return_port);
            return true;
        }
        if (have_default_iface()) {
            ping_req->error = strdup_printf("invalid return interface name: %s", ifname);
        } else {
            ping_req->error = strdup("need a return interface or a remote IP to send requests");
        }
        return false;
    }
    ping_req->node_id = oamif_get_uid(iface);
    ping_req->return_ip = strdup(oamif_get_ip(iface));
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
            ping_req->error = strdup("Error: ping options must be separated by space");
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
                ping_req->error = strdup("ping interval is invalid");
                opt_err = true;
                break;
            }
        } else if (c=='n') {
            if(!allow_num){
                ping_req->error = strdup("ping count is not allwed in config");
                opt_err = true;
                break;
            }
            k = sscanf(po, " %d%n", &val, &l);
            if (k == 1) {
                po += l;
                ping_req->count = val;
            } else {
                ping_req->error = strdup("ping count is invalid\n");
                opt_err = true;
                break;
            }
        } else if (c=='t') {
            k = sscanf(po, " %d%n", &val, &l);
            if (k == 1) {
                po += l;
                ping_req->ttl = val;
            } else {
                ping_req->error = strdup("ping ttl is invalid");
                opt_err = true;
                break;
            }
        } else {
            ping_req->error = strdup_printf("ping option '%c' is invalid", c);
            opt_err = true;
            break;
        }
    }
    if (opt_err) return false;
    while (isspace(*po)) po++;
    if (*po) {
        ping_req->error = strdup_printf("ping options '%s' is invalid", po);
        return false;
    }
    return true;
}

// always returns a request, sets ret->error to an error message
struct oam_request *parse_ping_command(const char *oam_command, bool allow_returniface, bool allow_num,
        char *conn_name)
{
    int l;
    char start_name[32];
    char iface_name[64];

    struct oam_request *ping_req = new_oam_request("ping", conn_name);

    if (oam_command[0]=='@') {
        if (!allow_returniface) {
            ping_req->error = strdup("ping return interface is not allowed");
            return ping_req;
        }
        int k = sscanf(oam_command, "@%s %s %s %d%n",
                       iface_name, start_name, ping_req->mep_stop, &ping_req->level, &l);
        if (k < 4) {
            ping_req->error = strdup("ping arguments invalid");
            return ping_req;
        }
    } else {
        int k = sscanf(oam_command, " %s %s %d%n",
                       start_name, ping_req->mep_stop, &ping_req->level, &l);
        if (k < 3) {
            ping_req->error = strdup("ping arguments invalid");
            return ping_req;
        }
        iface_name[0] = 0;
    }

    ping_req->mep_start = find_mep_start(start_name);
    if (ping_req->mep_start == NULL) {
        ping_req->error = strdup_printf("ping start '%s' invalid", start_name);
        return ping_req;
    }

    if (!parse_ping_returnif(ping_req, iface_name)) {
        return ping_req;
    }

    if (!parse_ping_options(ping_req, oam_command+l, allow_num)) {
        //TODO add something to the error?
    }

    return ping_req;
}

// always returns a request, sets ret->error to an error message
struct oam_request *parse_rping_command(const char *oam_command,
        char *conn_name)
{
    int l;
    char start_name[32];
    char iface_name[32];

    struct oam_request *rping_req = new_oam_request("rping", conn_name);

    if (oam_command[0]=='@') {
        int k = sscanf(oam_command, "@%s %s %s %d%n",
                       iface_name, start_name, rping_req->mep_stop, &rping_req->level, &l);
        if (k < 4) {
            rping_req->error = strdup("rping arguments invalid");
            return rping_req;
        }
    } else {
        int k = sscanf(oam_command, " %s %s %d%n",
                       start_name, rping_req->mep_stop, &rping_req->level, &l);
        if (k < 3) {
            rping_req->error = strdup("rping arguments invalid");
            return rping_req;
        }
        iface_name[0] = 0;
    }

    rping_req->mep_start = find_mep_start(start_name);
    if (rping_req->mep_start == NULL) {
        rping_req->error = strdup_printf("rping start '%s' invalid", start_name);
        return rping_req;
    }

    if (!parse_ping_returnif(rping_req, iface_name)) {
        return rping_req;
    }

    while (isspace(oam_command[l])) l++;
    rping_req->remote_command = strdup(oam_command+l);

    return rping_req;
}

// always returns a request, sets ret->error to an error message
struct oam_request *parse_rlist_command(const char *oam_command,
        char *conn_name)
{
    int l;
    char start_name[32];
    char iface_name[32];

    struct oam_request *rlist_req = new_oam_request("rlist", conn_name);

    if (oam_command[0]=='@') {
        int k = sscanf(oam_command, "@%s %s %s %d%n",
                       iface_name, start_name, rlist_req->mep_stop, &rlist_req->level, &l);
        if (k < 4) {
            rlist_req->error = strdup("rlist arguments invalid");
            return rlist_req;
        }
    } else {
        int k = sscanf(oam_command, " %s %s %d%n",
                       start_name, rlist_req->mep_stop, &rlist_req->level, &l);
        if (k < 3) {
            rlist_req->error = strdup("rlist arguments invalid");
            return rlist_req;
        }
        iface_name[0] = 0;
    }

    rlist_req->mep_start = find_mep_start(start_name);
    if (rlist_req->mep_start == NULL) {
        rlist_req->error = strdup_printf("rlist start '%s' invalid", start_name);
        return rlist_req;
    }

    if (!parse_ping_returnif(rlist_req, iface_name)) {
        return rlist_req;
    }

    while (isspace(oam_command[l])) l++;
    if (oam_command[l]) {
        rlist_req->error = strdup("rlist doesn't take so many arguments");
        return rlist_req;
    }

    return rlist_req;
}


struct oam_request *delete_oam_request(struct oam_request *req)
{
    if (req == NULL) return NULL;
    free(req->error);
    free(req->remote_command);
    free(req->return_ip);
    free(req->conn_name);
    free(req);
    return NULL;
}

const char *request_get_type(const struct oam_request *req)
{
    return req->type;
}

void request_set_error(struct oam_request *req, char *error)
{
    req->error = error;
}

const char *request_get_error(const struct oam_request *req)
{
    return req->error;
}

const char *request_get_stream_name(const struct oam_request *req)
{
    return req->mep_start->stream_name;
}

const char *request_get_start_name(const struct oam_request *req)
{
    return req->mep_start->name;
}

const char *request_get_stop_name(const struct oam_request *req)
{
    return req->mep_stop;
}

int request_get_level(const struct oam_request *req)
{
    return req->level;
}

void request_override_count(struct oam_request *req, unsigned count)
{
    req->count = count;
}

void request_set_return(struct oam_request *req, char *return_address, int return_port)
{
    req->return_ip = return_address;
    req->return_port = return_port;
}

const char *request_get_return_ip(const struct oam_request *req)
{
    return req->return_ip;
}

int request_get_return_port(const struct oam_request *req)
{
    return req->return_port;
}

void request_set_originator(struct oam_request *req, char *stream, unsigned char session_id)
{
    req->originator_stream = stream;
    req->originator_session_id = session_id;
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
    json_object_insert(js, "type", json_string(req->type));
    json_object_insert(js, "code", json_string("request"));
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
    timespec_to_tsntstamp(packet->timestamp, &packet->recv_time);

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

    log_packet("send request %s %s:%d seq %d lvl %d - %s",
               req->mep_start->name, req->mep_start->stream_name, req->session_id, req->seq, req->level,
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
    struct StreamSessions *stream = get_stream_sessions(req->mep_start->stream_name);

    while(1){
        req->seq = seq & 0xFF;
        send_request(req);
        session_touch(stream, req->session_id);
        seq++;
        if((req->count != 0) && (seq >= req->count)) break;
        usleep(req->interval_ms * 1000);
    }
    struct Thread *th = session_get_thread(stream, req->session_id);
    //TODO keep the session live until we receive the replies
    // stream->sessions[req->session_id].live = false;
    session_set_thread(stream, req->session_id, NULL);
    delete_oam_request(req);
    thread_exit(th);
    return NULL;
}

bool initiate_request(struct oam_request *req)
{
    struct command_connection *conn = find_command_connection(req->conn_name);
    FILE *cmd_w = command_connection_get_w(conn);

    struct Pipeline *pipe = req->mep_start->pipe;
    if (!pipe) {
        req->error = strdup_printf("mep start '%s' has no pipeline!?!\n", req->mep_start->name);
        if (cmd_w) fprintf(cmd_w, "%s", req->error);
        command_connection_release_w(conn);
        return false;
    }

    struct StreamSessions *stream = get_stream_sessions(req->mep_start->stream_name);
    int session_id = alloc_session_id(stream, req, req->conn_name, req->interval_ms);
    if (session_id < 0) {
        req->error = strdup_printf("stream %s has no free session id\n", req->mep_start->stream_name);
        if (cmd_w) fprintf(cmd_w, "%s", req->error);
        command_connection_release_w(conn);
        return false;
    }

    req->session_id = session_id;
    req->seq = 0;

    log_info("request %s stream %s:%d seq %d lvl %d type %s mep %s -> %s"
            " count %d interval %d, rr: %s os: %s [reply to ip: %s, port: %u]",
             req->mep_start->name, req->mep_start->stream_name, req->session_id,
             req->seq, req->level, req->type, req->mep_start->name, req->mep_stop, req->count, req->interval_ms,
             req->record_route?"yes":"no", req->object_state?"yes":"no", req->return_ip, req->return_port);

    if (cmd_w) fprintf(cmd_w, "OAM request %s session %u seq %u, %s -> %s level %d count %d interval %d,"
            " rr: %s os: %s\t[reply to ip: %s, port: %u]\n",
            req->type, req->session_id, req->seq, req->mep_start->name, req->mep_stop, req->level, req->count, req->interval_ms,
            req->record_route?"yes":"no", req->object_state?"yes":"no", req->return_ip, req->return_port);

    if(req->count == 1){
        session_set_thread(stream, session_id, NULL);
        send_request(req);
        delete_oam_request(req);
    } else {
        session_set_thread(stream, session_id, thread_launch(oam_request_thread, req, "oam req %d", session_id));
        if (session_get_thread(stream, session_id) == NULL) {
            req->error = strdup("could not create new ping thread");
            log_error("%s", req->error);
            if (cmd_w) fprintf(cmd_w, "%s", req->error);
            command_connection_release_w(conn);
            return false;
        }
    }

    command_connection_release_w(conn);
    return true;
}



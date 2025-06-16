// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#define OAM_INTERNAL

#include "oam_request.h"
#include "oam_command.h"
#include "oam_core.h"
#include "oam_maintenance.h"
#include "oam_session.h"

#include "if_oam.h"
#include "inet_utils.h"
#include "json.h"
#include "log.h"
#include "notification.h"
#include "oam.h"
#include "packet.h"
#include "replicate.h"
#include "state.h"
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
#define OAM_CHANNEL 0x7fff /* Experimental channel type, we are not compatible with anything */

struct OamRequest {
    const char *type;
    char *conn_name; // NULL if not issued from a command connection
    struct JsonValue *return_addr;

    unsigned char ttl;
    unsigned char session_id;
    unsigned char seq;
    unsigned char level;
    unsigned node_id; // 20 bit

    struct OAM_MaintenancePoint *mp_start;
    char mep_stop[32]; //TODO target (or destination?)

    // ping options
    bool record_route;
    bool object_state;
    bool measure_delay;
    unsigned count;
    unsigned interval_ms;

    // rping options
    char *remote_command; // rping carries this ping command string
    char *originator_stream; // for ping initiated by rping
    unsigned originator_session_id; // for ping initiated by rping

    char *error;
};

DEFAULT_LOGGING_MODULE(OAM, INFO);

// @conn_name will be owned by the request
static struct OamRequest *new_oam_request(const char *type, const char *conn_name)
{
    struct OamRequest *req = calloc_struct(OamRequest);

    req->conn_name = conn_name ? strdup(conn_name) : NULL; //TODO when do we get NULL?
    req->type = type;
    req->ttl = OAM_PING_TTL;
    req->count = 1;
    req->interval_ms = 1000;
    req->return_addr = json_object();
    req->node_id = get_default_node_id();

    return req;
}

static bool parse_returnif(struct OamRequest *req, const char *ifname)
{
    struct Interface *iface = get_oam_interface(ifname);
    if (iface == NULL) {
        // not an interface name, try interpreting as ip:port
        char *ip = NULL;
        unsigned port = OAM_PORT;
        if (parse_ip_port(ifname, &ip, &port)) {
            json_object_insert(req->return_addr, "ip", json_string(ip));
            json_object_insert(req->return_addr, "port", json_number(port));
            log_debug("return ip '%s' port %u", ip, port);
            free(ip);
            return true;
        } else {
            // not interface name, not ip:port, try interpreting as mac+vlan
            char *mac = NULL;
            unsigned vlan = 0;
            if (parse_mac_vlan(ifname, &mac, &vlan)) {
                json_object_insert(req->return_addr, "mac", json_string(mac));
                if (vlan)
                    json_object_insert(req->return_addr, "vlan", json_number(vlan));
                log_debug("return mac '%s' vlan %u", mac, vlan);
                free(mac);
                return true;
            }
        }

        if (have_default_iface()) {
            req->error = strdup_printf("invalid return interface name: %s", ifname);
        } else {
            req->error = strdup("config has no return interface, need to specify a return IP to send requests");
        }
        return false;
    } else {
        //TODO depends on the type of the return interface
        json_object_insert(req->return_addr, "ip", json_string(oamif_get_ip(iface)));
        json_object_insert(req->return_addr, "port", json_number(oamif_get_port(iface)));
        return true;
    }
}

static bool parse_ping_options(struct OamRequest *ping_req, const char *options_str, bool allow_num)
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
            ping_req->measure_delay = true;
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
                ping_req->error = strdup("ping count is invalid");
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
struct OamRequest *parse_ping_command(const char *oam_command, bool allow_returniface, bool allow_num,
        const char *conn_name)
{
    int l;
    char start_name[32];
    char iface_name[64];

    struct OamRequest *ping_req = new_oam_request("ping", conn_name);

    if (oam_command[0]=='@') {
        if (!allow_returniface) {
            ping_req->error = strdup("ping return interface is not allowed");
            return ping_req;
        }
        int k = sscanf(oam_command, "@%s %s %s %hhd%n",
                       iface_name, start_name, ping_req->mep_stop, &ping_req->level, &l);
        if (k < 4) {
            ping_req->error = strdup("ping arguments invalid");
            return ping_req;
        }
    } else {
        int k = sscanf(oam_command, " %s %s %hhd%n",
                       start_name, ping_req->mep_stop, &ping_req->level, &l);
        if (k < 3) {
            ping_req->error = strdup("ping arguments invalid");
            return ping_req;
        }
        iface_name[0] = 0;
    }

    ping_req->mp_start = find_maintenance_point(start_name);
    if (ping_req->mp_start == NULL) {
        ping_req->error = strdup_printf("ping start '%s' invalid", start_name);
        return ping_req;
    }
    if (!mp_can_send(ping_req->mp_start)) {
        ping_req->error = strdup_printf("ping start '%s' can't send", start_name);
        return ping_req;
    }

    if (!parse_returnif(ping_req, iface_name)) {
        return ping_req;
    }

    if (!parse_ping_options(ping_req, oam_command+l, allow_num)) {
        //TODO add something to the error?
    }

    return ping_req;
}

// always returns a request, sets ret->error to an error message
struct OamRequest *parse_rping_command(const char *oam_command,
        const char *conn_name)
{
    int l;
    char start_name[32];
    char iface_name[32];

    struct OamRequest *rping_req = new_oam_request("rping", conn_name);

    if (oam_command[0]=='@') {
        int k = sscanf(oam_command, "@%s %s %s %hhd%n",
                       iface_name, start_name, rping_req->mep_stop, &rping_req->level, &l);
        if (k < 4) {
            rping_req->error = strdup("rping arguments invalid");
            return rping_req;
        }
    } else {
        int k = sscanf(oam_command, " %s %s %hhd%n",
                       start_name, rping_req->mep_stop, &rping_req->level, &l);
        if (k < 3) {
            rping_req->error = strdup("rping arguments invalid");
            return rping_req;
        }
        iface_name[0] = 0;
    }

    rping_req->mp_start = find_maintenance_point(start_name);
    if (rping_req->mp_start == NULL) {
        rping_req->error = strdup_printf("rping start '%s' invalid", start_name);
        return rping_req;
    }
    if (!mp_can_send(rping_req->mp_start)) {
        rping_req->error = strdup_printf("rping start '%s' can't send", start_name);
        return rping_req;
    }

    if (!parse_returnif(rping_req, iface_name)) {
        return rping_req;
    }

    while (isspace(oam_command[l])) l++;
    rping_req->remote_command = strdup(oam_command+l);

    return rping_req;
}


static bool parse_trigger_options(struct OamRequest *trig_req, const char *options_str, bool allow_num)
{
    const char *po = options_str;
    bool opt_err = false;
    int k, l;
    int val;
    float fval;
    char c;

    while ((k=sscanf(po, " -%c%n", &c, &l)) == 1) {
        if (!isspace(*po)) {
            trig_req->error = strdup("Error: trigger options must be separated by space");
            opt_err = true;
            break;
        }
        po += l;
        if (c=='i') {
            k = sscanf(po, " %f%n", &fval, &l);
            if (k == 1) {
                po += l;
                if (fval < 0.002) fval = 0.002; // 2msec is the minimum
                trig_req->interval_ms = fval * 1000;
            } else {
                trig_req->error = strdup("trigger interval is invalid");
                opt_err = true;
                break;
            }
        } else if (c=='n') {
            if(!allow_num){
                trig_req->error = strdup("trigger count is not allwed in config");
                opt_err = true;
                break;
            }
            k = sscanf(po, " %d%n", &val, &l);
            if (k == 1) {
                po += l;
                trig_req->count = val;
            } else {
                trig_req->error = strdup("trigger count is invalid\n");
                opt_err = true;
                break;
            }
        } else if (c=='t') {
            k = sscanf(po, " %d%n", &val, &l);
            if (k == 1) {
                po += l;
                trig_req->ttl = val;
            } else {
                trig_req->error = strdup("trigger ttl is invalid");
                opt_err = true;
                break;
            }
        } else {
            trig_req->error = strdup_printf("trigger option '%c' is invalid", c);
            opt_err = true;
            break;
        }
    }
    if (opt_err) return false;
    while (isspace(*po)) po++;
    if (*po) {
        trig_req->error = strdup_printf("trigger options '%s' is invalid", po);
        return false;
    }
    return true;
}

struct OamRequest *parse_trigger_command(const char *oam_command, bool allow_num,
        const char *conn_name)
{
    int l;
    char start_name[32];

    struct OamRequest *trig_req = new_oam_request("trigger", conn_name);

    int k = sscanf(oam_command, " %s %s %hhd%n",
                   start_name, trig_req->mep_stop, &trig_req->level, &l);
    if (k < 3) {
        trig_req->error = strdup("notif_trigger arguments invalid");
        return trig_req;
    }

    trig_req->mp_start = find_maintenance_point(start_name);
    if (trig_req->mp_start == NULL) {
        trig_req->error = strdup_printf("notif_trigger start '%s' invalid", start_name);
        return trig_req;
    }
    if (!mp_can_send(trig_req->mp_start)) {
        trig_req->error = strdup_printf("notif_trigger start '%s' can't send", start_name);
        return trig_req;
    }

    if (!parse_trigger_options(trig_req, oam_command+l, allow_num)) {
        //TODO add something to the error?
    }

    trig_req->node_id = get_default_node_id();

    return trig_req;
}

// always returns a request, sets ret->error to an error message
struct OamRequest *parse_rlist_command(const char *oam_command,
        const char *conn_name)
{
    int l;
    char start_name[32];
    char iface_name[32];

    struct OamRequest *rlist_req = new_oam_request("rlist", conn_name);

    if (oam_command[0]=='@') {
        int k = sscanf(oam_command, "@%s %s %s %hhd%n",
                       iface_name, start_name, rlist_req->mep_stop, &rlist_req->level, &l);
        if (k < 4) {
            rlist_req->error = strdup("rlist arguments invalid");
            return rlist_req;
        }
    } else {
        int k = sscanf(oam_command, " %s %s %hhd%n",
                       start_name, rlist_req->mep_stop, &rlist_req->level, &l);
        if (k < 3) {
            rlist_req->error = strdup("rlist arguments invalid");
            return rlist_req;
        }
        iface_name[0] = 0;
    }

    rlist_req->mp_start = find_maintenance_point(start_name);
    if (rlist_req->mp_start == NULL) {
        rlist_req->error = strdup_printf("rlist start '%s' invalid", start_name);
        return rlist_req;
    }
    if (!mp_can_send(rlist_req->mp_start)) {
        rlist_req->error = strdup_printf("rlist start '%s' can't send", start_name);
        return rlist_req;
    }

    if (!parse_returnif(rlist_req, iface_name)) {
        return rlist_req;
    }

    while (isspace(oam_command[l])) l++;
    if (oam_command[l]) {
        rlist_req->error = strdup("rlist doesn't take so many arguments");
        return rlist_req;
    }

    return rlist_req;
}

struct OamRequest *parse_mask_command(const char *oam_command, const char *conn_name)
{
    struct OamRequest *mask_req = NULL;
    bool new_mask = true;
    char pipename[64] = { 0 };
    int seek = 0;
    if (strncmp(oam_command, "unmask", 6) == 0) {
        mask_req = new_oam_request("unmask", conn_name);
        mask_req->count = 1;
        seek = 2;
        new_mask = false;
    } else {
        mask_req = new_oam_request("mask", conn_name);
        mask_req->count = 0; // heartbeat until unmask
    }
    sprintf(mask_req->mep_stop, "nexthop");

    // called by mask_checker_thread (the re-generator codepath, no CLI or repl obj involved)
    // in that case we dont have CLI session or error cases
    if (conn_name == NULL) {
        return mask_req;
    }

    if(sscanf(oam_command+seek, "mask %s", pipename) != 1) {
        mask_req->error = strdup_printf("mask command is invalid. Format: [un]mask PIPENAME");
        return mask_req;
    }

    struct PipelineObject *repl = NULL;
    struct Pipeline *pipe = replicate_lookup_pipeline(pipename, &repl);
    if (pipe) {
        bool changed = pipe_set_mask(pipe, new_mask);
        if (changed == false) {
            mask_req->error = strdup_printf("pipeline already %sed", mask_req->type);
            return mask_req;
        }

        struct CommandConnection *conn = find_command_connection(conn_name);
        FILE *cmd_w = command_connection_get_w(conn);
        fprintf(cmd_w, "Pipeline '%s' %sed\n", pipename, mask_req->type);
        release_command_connection(conn);

        char *postmip_name = strdup_printf("o_%s_L%u_post-%s", pipename, repl->auto_mip_level, repl->name);
        mask_req->mp_start = find_maintenance_point(postmip_name);
    } else {
        mask_req->error = strdup_printf("replication pipeline '%s' not found", pipename);
        return mask_req;
    }
    mask_req->level = repl->auto_mip_level;
    return mask_req;
}


struct OamRequest *delete_oam_request(struct OamRequest *req)
{
    if (req == NULL) return NULL;
    if (req->mp_start)
        oam_unref_maintenance_point(req->mp_start);
    json_delete(req->return_addr);
    free(req->error);
    free(req->remote_command);
    free(req->conn_name);
    free(req);
    return NULL;
}

const char *request_get_start_name(const struct OamRequest *req)
{
    return mp_get_name(req->mp_start);
}

const char *request_get_stop_name(const struct OamRequest *req)
{
    return req->mep_stop;
}

const char *request_get_stream_name(const struct OamRequest *req)
{
    return mp_get_stream_name(req->mp_start);
}

const char *request_get_type(const struct OamRequest *req)
{
    return req->type;
}

const char *request_get_error(const struct OamRequest *req)
{
    return req->error;
}

unsigned request_get_session_id(const struct OamRequest *req)
{
    return req->session_id;
}

int request_get_level(const struct OamRequest *req)
{
    return req->level;
}

char *request_get_return_addr_string(const struct OamRequest *req)
{
    struct JsonValue *ip = json_object_get_string(req->return_addr, "ip");
    struct JsonValue *port = json_object_get_number(req->return_addr, "port");
    struct JsonValue *dmac = json_object_get_string(req->return_addr, "dmac");
    struct JsonValue *vlan = json_object_get_number(req->return_addr, "vlan");

    if (ip && port) {
        return strdup_printf("[reply to ip %s port %g]", ip->v.string, port->v.number);
    } else if (dmac && vlan) {
        return strdup_printf("[reply to mac %s vlan %g]", dmac->v.string, vlan->v.number);
    } else if (dmac) {
        return strdup_printf("[reply to mac %s]", dmac->v.string);
    } else {
        return strdup("[no reply address]");
    }
}

void request_get_identification_data(const struct OamRequest *req, unsigned *nodeid,
        unsigned char *level, unsigned char *session,
        unsigned char *seq, unsigned char *ttl)
{
    *nodeid = req->node_id;
    *level = req->level;
    *session = req->originator_stream ? req->originator_session_id : req->session_id;
    *seq = req->seq;
    *ttl = req->ttl;
}

void request_set_error(struct OamRequest *req, char *error)
{
    req->error = error;
}

void request_set_infinite_count(struct OamRequest *req)
{
    req->count = 0;
}

void request_set_return_addr(struct OamRequest *req, struct JsonValue *addr)
{
    json_delete(req->return_addr);
    req->return_addr = addr;
}

void request_set_originator(struct OamRequest *req, const char *stream, unsigned char session_id)
{
    free(req->originator_stream);
    req->originator_stream = stream ? strdup(stream) : NULL;
    req->originator_session_id = session_id;
}

static void trigger_mep_push_notification(const struct OamRequest *req)
{
    unsigned session_id = req->originator_stream ? req->originator_session_id : req->session_id;

    struct JsonValue *js = json_object();
    json_object_insert(js, "seq", json_number(req->seq));
    json_object_insert(js, "level", json_number(req->level));
    json_object_insert(js, "node_id", json_number(req->node_id));
    json_object_insert(js, "session", json_number(session_id));
    json_object_insert(js, "source", json_string(mp_get_name(req->mp_start)));
    json_object_insert(js, "stream", json_string(mp_get_stream_name(req->mp_start)));
    json_object_insert(js, "target", json_string(req->mep_stop));

    struct JsonValue *jlist = mp_get_state_json_by_object(req->mp_start);
    json_object_insert(js, "mp", jlist);

    notification_push_event("triggered_source", NOTIF_INFO, js);
}

// returns true on success
static bool send_request(const struct OamRequest *req)
{
    struct Packet *packet = new_packet(NULL);
    struct JsonValue *js = mp_pack_message(req->mp_start, packet, req);

    if (req->originator_stream) {
        json_object_insert(js, "stream", json_string(req->originator_stream));
    } else {
        json_object_insert(js, "stream", json_string(mp_get_stream_name(req->mp_start)));
    }
    json_object_insert(js, "target", json_string(req->mep_stop));

    if (strcmp(req->type, "trigger")==0) {
        json_object_insert(js, "seq", json_number(req->seq));
        json_object_insert(js, "source", mp_get_state_json(req->mp_start, false));

        // we also triger local notification
        trigger_mep_push_notification(req);
    } else {
        //TODO mask signal doesn't need "return" either
        json_object_insert(js, "return", json_duplicate(req->return_addr));
    }

    if (strcmp(req->type, "ping")==0) {
        if (req->record_route) {
            struct JsonValue *jrr = json_array();
            json_array_unshift(jrr, json_string(mp_get_name(req->mp_start)));
            json_object_insert(js, "rr", jrr);
        }
        if (req->object_state) {
            json_object_insert(js, "object", json_true());
        }
        if (req->measure_delay) {
            json_object_insert(js, "delay", json_true());
        }
    }
    if (strcmp(req->type, "rping")==0) {
        //TODO this hardcodes the commandline format into the protocol :(
        json_object_insert(js, "command", json_string(req->remote_command));
    }

    unsigned js_length;
    char *js_string;
    js_string = json_serialize(js, &js_length);
    json_delete(js);
    if (js_string == NULL) {
        delete_packet(packet);
        return false;
    }
    //TODO don't hardcode 2
    packet_add_header(packet, 2, PROTO_ID_PAYLOAD, js_length);
    unsigned char *msg = packet->buf + packet->headers[2].start;
    memcpy(msg, js_string, js_length);

    log_packet("send request %s %s:%d seq %d lvl %d - %s",
               mp_get_name(req->mp_start), mp_get_stream_name(req->mp_start), req->session_id, req->seq, req->level,
               js_string);

    free(js_string);

    mp_inject_packet(req->mp_start, packet);
    return true;
}

static void *send_periodic_request_thread(void *arg)
{
    struct OamRequest *req = (struct OamRequest *)arg;
    unsigned seq = 0;
    struct StreamSessions *stream = get_stream_sessions(mp_get_stream_name(req->mp_start));

    while (1) {
        req->seq = seq & 0xFF;
        send_request(req);
        session_touch(stream, req->session_id);
        seq++;
        if((req->count != 0) && (seq >= req->count)) break;
        usleep(req->interval_ms * 1000);
    }

    struct Thread *th = session_get_thread(stream, req->session_id);
    // we keep the session live so we can still receive replies
    session_set_thread(stream, req->session_id, NULL);
    thread_exit(th);
    return NULL;
}

bool initiate_request(struct OamRequest *req)
{
    if (!req->mp_start) { //TODO this can only happen for mask
        req->error = strdup_printf("can't initiate %s request without start maintenance point", req->type);
        return false;
    }

    if (!mp_can_send(req->mp_start)) {
        req->error = strdup_printf("mep start '%s' cannot send", mp_get_name(req->mp_start));
        return false;
    }

    struct StreamSessions *stream = get_stream_sessions(mp_get_stream_name(req->mp_start));
    int session_id = alloc_session_id(stream, req, req->conn_name, req->interval_ms);
    if (session_id < 0) {
        req->error = strdup_printf("stream %s has no free session id", mp_get_stream_name(req->mp_start));
        return false;
    }

    req->session_id = session_id;
    req->seq = 0;

    char *return_str = request_get_return_addr_string(req);
    log_info("request %s stream %s:%d seq %d lvl %d type %s mep %s -> %s"
            " count %d interval %d, rr: %s os: %s %s",
             mp_get_name(req->mp_start), mp_get_stream_name(req->mp_start), req->session_id,
             req->seq, req->level, req->type, mp_get_name(req->mp_start), req->mep_stop, req->count, req->interval_ms,
             req->record_route?"yes":"no", req->object_state?"yes":"no", return_str);

    struct CommandConnection *conn = find_command_connection(req->conn_name);
    FILE *cmd_w = command_connection_get_w(conn);
    if (cmd_w) fprintf(cmd_w, "OAM request %s session %u seq %u, %s -> %s level %d count %d interval %d,"
            " rr: %s os: %s\t%s\n",
            req->type, req->session_id, req->seq, mp_get_name(req->mp_start),
            req->mep_stop, req->level, req->count, req->interval_ms,
            req->record_route?"yes":"no", req->object_state?"yes":"no", return_str);
    free(return_str);
    release_command_connection(conn);

    if (req->count == 1) {
        session_set_thread(stream, session_id, NULL);
        send_request(req);
    } else {
        struct Thread *th = thread_launch(send_periodic_request_thread, req, "oam req %d", session_id);
        session_set_thread(stream, session_id, th);
        if (th == NULL) {
            req->error = strdup("could not create new request thread");
            log_error("%s", req->error);
            return false;
        }
    }

    return true;
}

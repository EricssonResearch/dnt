// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#define OAM_INTERNAL

#include "oam_message.h"
#include "oam_command.h"
#include "oam_core.h"
#include "oam_maintenance.h"
#include "oam_session.h"

#include "if_udp_out.h"
#include "json.h"
#include "log.h"
#include "notification.h"
#include "oam.h"
#include "object.h"
#include "packet.h"
#include "pipeline.h"
#include "state.h"
#include "thread_utils.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <arpa/inet.h>


DEFAULT_LOGGING_MODULE(OAM, INFO);

static struct Thread *request_thread = NULL;
static struct MessageQueue *request_q = NULL;
static struct Thread *reply_thread = NULL;
static struct MessageQueue *reply_q = NULL;

static struct Thread *inband_receiver_thread = NULL;
static struct MessageQueue *inband_q = NULL;
static struct Thread *outofband_receiver_thread = NULL;
static struct MessageQueue *outofband_q = NULL;


//TODO new module for the MP state
struct OamEndPoint {
    char *name;
    char *stream;
    int level;
    bool stop; // false: MIP, true: MEP-Stop
    struct MepStart *mep;
};

struct OamEndPoint *oam_create_endpoint(const char *name, const char *stream, int level, bool stop)
{
    //TODO make sure that endpoints have unique names
    //      put them into the same hash as the startpoints?
    struct OamEndPoint *ret = calloc_struct(OamEndPoint);
    ret->name = strdup(name);
    ret->stream = strdup(stream);
    ret->level = level;
    ret->stop = stop;
    return ret;
}

struct OamEndPoint *oam_delete_endpoint(struct OamEndPoint *end)
{
    free(end->name);
    free(end->stream);
    free(end);
    return NULL;
}

static int process_reply(const char *msg)
{
#define THROW(msg, ...)                     \
    do {                                    \
        log_error(msg, ##__VA_ARGS__);      \
        json_delete(j);                     \
        release_command_connection(conn);   \
        return -1;                          \
    } while (0)

#define JS_OBJECT_GET(_json, _key, _type)                                   \
    struct JsonValue *_json##_key = json_object_get_##_type(_json, #_key);  \
    if (_json##_key == NULL) {                                              \
        THROW("No " #_key " in reply message.");                            \
    }

    char reply_str[1400], rr_str[512];
    char *jerr;
    struct JsonValue *j = json_parse(msg, strlen(msg), &jerr);
    if (j == NULL || j->type != JSON_OBJECT) {
        log_error("JSON in reply is invalid: %s", jerr);
        free(jerr);
        return -1;
    }

    struct CommandConnection *conn = NULL;
    FILE *cmd_w = NULL;

    JS_OBJECT_GET(j, type, string);

    if (strcmp(jtype->v.string, "newaddress") == 0) {
        JS_OBJECT_GET(j, code, string);
        if (strcmp(jcode->v.string, "notify") != 0) {
            THROW("newaddress message is '%s' instead of 'notify'", jcode->v.string);
        }

        JS_OBJECT_GET(j, sendiface, string);
        struct Interface *sendif = state_get_interface(jsendiface->v.string);
        if (sendif == NULL) {
            THROW("got newaddress notification for non-existing interface '%s'", jsendiface->v.string);
        }

        JS_OBJECT_GET(j, address, object);
        JS_OBJECT_GET(jaddress, ip, string);
        JS_OBJECT_GET(jaddress, port, number);
        log_debug("newaddress notification for %s is %s %.0f",
                jsendiface->v.string, jaddressip->v.string, jaddressport->v.number);
        if (udp_out_set_dst(sendif, jaddressip->v.string, jaddressport->v.number)) {
            json_delete(j);
            return 0;
        } else {
            json_delete(j);
            return -1;
        }
    }

    JS_OBJECT_GET(j, nodeid, number);
    JS_OBJECT_GET(j, target, string);
    JS_OBJECT_GET(j, seq, number);
    JS_OBJECT_GET(j, level, number);
    JS_OBJECT_GET(j, receiver, object);
    JS_OBJECT_GET(j, stream, string);
    JS_OBJECT_GET(j, session, number);
    JS_OBJECT_GET(jreceiver, name, string);

    log_packet("recv reply %s:%.0f seq %.0f lvl %.0f - %s",
            jstream->v.string, jsession->v.number, jseq->v.number, jlevel->v.number, msg);

    if (jsession->v.number < 0 || jsession->v.number > 15) {
        THROW("session id %.0f in reply is invalid", jsession->v.number);
    }

    conn = command_connection_for_session(jstream->v.string, jsession->v.number);
    if (conn)
        cmd_w = command_connection_get_w(conn);

    if (strcmp(jtype->v.string, "rlist") == 0) {
        JS_OBJECT_GET(j, code, string);
        if (strcmp(jcode->v.string, "reply") != 0) {
            THROW("rlist result is not a reply.");
        }

        JS_OBJECT_GET(j, list, array);
        sprintf(reply_str, "Rlist result from %s:\n", jreceivername->v.string);
        for (unsigned i=0; i<json_array_size(jlist); i++) {
            struct JsonValue *str = json_array_at(jlist, i);
            if (str->type != JSON_STRING) {
                THROW("rlist result is not string.");
            }
            strcat(reply_str, str->v.string);
            strcat(reply_str, "\n");
        }
        json_delete(j);
        if (cmd_w) fprintf(cmd_w, "%s\n", reply_str);
    }
    else if (strcmp(jtype->v.string, "rping") == 0) {
        JS_OBJECT_GET(j, code, string);
        if (strcmp(jcode->v.string, "error") != 0) {
            THROW("rping response is not error.");
        }

        JS_OBJECT_GET(j, error, string);
        snprintf(reply_str, sizeof(reply_str), "Rping error from %s : %s\n", jreceivername->v.string, jerror->v.string);
        json_delete(j);
        if (cmd_w) fprintf(cmd_w, "%s\n", reply_str);
    }
    else if (strcmp(jtype->v.string, "ping") == 0) {
        JS_OBJECT_GET(j, code, string);
        if (strcmp(jcode->v.string, "reply") != 0) {
            THROW("ping result is not a reply.");
        }

        struct JsonValue *delay = json_object_get_bool(j, "delay");
        if (delay != NULL && delay->type == JSON_TRUE) {
            // calculate delay
            JS_OBJECT_GET(j, send_s, number);
            JS_OBJECT_GET(j, send_ns, number);
            JS_OBJECT_GET(j, recv_s, number);
            JS_OBJECT_GET(j, recv_ns, number);
            struct timespec sendtime, receivetime, delay_diff;
            sendtime.tv_sec = jsend_s->v.number;
            sendtime.tv_nsec = jsend_ns->v.number;
            receivetime.tv_sec = jrecv_s->v.number;
            receivetime.tv_nsec = jrecv_ns->v.number;
            timespecsub(&receivetime, &sendtime, &delay_diff);

            sprintf(reply_str,"  oam_r %s:%.0f seq %.0f lvl %.0f R - %s on stream %s target %s; reply from %s delay %ld.%09ld",
                    jstream->v.string, jsession->v.number, jseq->v.number, jlevel->v.number,
                    jtype->v.string, jstream->v.string, jtarget->v.string, jreceivername->v.string,
                    delay_diff.tv_sec, delay_diff.tv_nsec);
        }
        else
            sprintf(reply_str,"  oam_r %s:%.0f seq %.0f lvl %.0f R - %s on stream %s target %s; reply from %s",
                    jstream->v.string, jsession->v.number, jseq->v.number, jlevel->v.number,
                    jtype->v.string, jstream->v.string, jtarget->v.string, jreceivername->v.string);

        // Recorded route is single line, no difference between log/dump
        struct JsonValue *jrr = json_object_get_array(j, "rr");
        rr_str[0] = 0;
        if(jrr){
            sprintf(rr_str, "Record Route: [");
            for (unsigned i=0; i<json_array_size(jrr); i++) {
                struct JsonValue *rritem = json_array_at(jrr, i);
                if (rritem->type != JSON_STRING) {
                    THROW("record route item is not string");
                }
                strcat(rr_str, " ");
                strcat(rr_str, rritem->v.string);
            }
            strcat(rr_str, " ]");
        }

        // different format between log/dump
        char *obj_str = NULL;
        char *obj_str_log = NULL;
        struct JsonValue *o_info = json_object_get_object(jreceiver, "object");
        if (o_info) {
            obj_str = sprintf_state_json(o_info, ", ", "\n\t\t");
            obj_str_log = sprintf_state_json(o_info, ", ", "; ");
        }
        if (obj_str_log == NULL) obj_str_log = strdup("");

        // Logging
        log_info("%s %s %s", reply_str, rr_str, obj_str_log);
        free(obj_str_log);

        j = json_delete(j);

        // check if we need to print to a telnet session
        if (conn == NULL) return 0; // this is a background ping

        if (command_connection_get_format(conn) == TF_JSON) {
            fprintf(cmd_w, "%s\n", msg);
        } else { // DUMP mode
            if (rr_str[0]) {
                strcat(reply_str, "\n\t");
                strcat(reply_str, rr_str);
            }
            if (obj_str) {
                strcat(reply_str, "\n\t");
                strcat(reply_str, obj_str);
                free(obj_str);
            }
            fprintf(cmd_w, "%s\n", reply_str);
        }
    }
    else {
        THROW("invalid reply type '%s'", jtype->v.string);
    }
    release_command_connection(conn);
    return 0;
#undef JS_OBJECT_GET
#undef THROW
}

static void *reply_thread_fn(void *arg)
{
    (void)arg;

    while (1) {
        char *msg = (char *)messagequeue_pop(reply_q, -1);
        if (msg == NULL)
            return NULL;

        process_reply(msg);
        free(msg);
    }

    return NULL;
}

void oam_recv_reply(const char *msg)
{
    // @msg is an on-stack buffer in if_oam recv
    messagequeue_push(reply_q, strdup(msg));
}

/*
 * Send UDP OAM reply mesage
 * Address: destination address string (can be either IPV4, IPv6, or FQDN)
 * Msg: pointer to the message
 * Return 0 on success
*/
static int oam_send_reply(const char *address, unsigned port, const char *msg, unsigned msg_len)
{
    struct in_addr dst4;
    struct in6_addr dst6;
    int family;

    if (inet_pton(AF_INET, address, &dst4) == 1) {
        family = AF_INET;
    } else if (inet_pton(AF_INET6, address, &dst6) == 1) {
        family = AF_INET6;
    } else {
        log_error("oam_send_reply invalid destination '%s'", address);
        return -1;
    }

    struct sockaddr *sa;
    unsigned sa_len;
    struct sockaddr_in6 addr6;
    struct sockaddr_in addr4;

    if (family == AF_INET6) {
        sa = (struct sockaddr*)&addr6;
        sa_len = sizeof(addr6);
        memset(&addr6, 0, sizeof(addr6));
        addr6.sin6_family = AF_INET6;
        addr6.sin6_addr = dst6;
        addr6.sin6_port = htons(port);
    } else {
        sa = (struct sockaddr*)&addr4;
        sa_len = sizeof(addr4);
        memset(&addr4, 0, sizeof(addr4));
        addr4.sin_family = AF_INET;
        addr4.sin_addr = dst4;
        addr4.sin_port = htons(port);
    }

    int sock = socket(family, SOCK_DGRAM, 0);
    if (sock < 0) {
        log_perror("oam_send_reply cannot create socket");
        return -1;
    }

    if (sendto(sock, msg, msg_len, 0, sa, sa_len) <= 0) {
        log_perror("oam repy sendto");
        close(sock);
        return -1;
    }
    close(sock);
    return 0;
}

// @returns false on error
static bool get_return_ip_port(struct JsonValue *j, char **reply_address, int *port)
{
    struct JsonValue *jret = json_object_get_object(j, "return");
    if (jret==NULL) {
        log_error("OAM packet has no return address");
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

    INTERPRET_DACH(p->buf + p->headers[1].start);

    if (!get_return_ip_port(j, &reply_address, &port)) {
        json_delete(j);
        return false;
    }

    // if object state is requested
    struct JsonValue *jos = json_object_get_any(j, "object");
    if(jos!=NULL){
        struct MepStart *mep = find_mep_start(oam->name);
        if (mep && mep->target) {
            struct JsonValue *objinfo = mep->target->get_state(mep->target);
            json_object_insert(j, "object", objinfo);
        }
        if (mep) {
            struct JsonValue *jmepstate = json_object();
            json_object_insert(jmepstate, "packets_passed", json_number(mep->packets_passed));
            json_object_insert(jmepstate, "octets_passed", json_number(mep->octets_passed));
            json_object_insert(jmepstate, "oam_packets_passed", json_number(mep->oam_packets_passed));
            json_object_insert(jmepstate, "oam_octets_passed", json_number(mep->oam_octets_passed));
            json_object_insert(jmepstate, "name", json_string(mep->name));
            json_object_insert(j, "target_info", jmepstate);
        }
    }

    json_object_remove(j, "return");
    json_object_insert(j, "code", json_string("reply"));
    json_object_insert(j, "sequence", json_number(dach.seq));
    json_object_insert(j, "level", json_number(dach.level));
    json_object_insert(j, "nodeid", json_number(dach.nodeid));
    json_object_insert(j, "session", json_number(dach.session));
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
    if (j_msg) {
        log_packet("send ping reply %s %s:%d seq %d lvl %d (to %s %d) - %s",
                oam->name, stream, dach.session, dach.seq, dach.level,
                reply_address, port, j_msg);

        oam_send_reply(reply_address, port, j_msg, msg_len);
        free(j_msg);
    }
    free(reply_address);
    json_delete(j);
    return j_msg != NULL;
}

static bool send_rping_error(struct OamEndPoint *oam, struct Packet *p, struct JsonValue *j,
        struct OamRequest *ping_req)
{
    INTERPRET_DACH(p->buf + p->headers[1].start);

    json_object_remove(j, "return");
    json_object_insert(j, "code", json_string("error"));
    json_object_insert(j, "sequence", json_number(dach.seq));
    json_object_insert(j, "level", json_number(dach.level));
    json_object_insert(j, "nodeid", json_number(dach.nodeid));
    json_object_insert(j, "session", json_number(dach.session));
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

    json_object_insert(j, "error", json_string(request_get_error(ping_req)));

    unsigned msg_len=0;
    char *j_msg = json_serialize(j, &msg_len);
    if (j_msg) {
        log_packet("send rping error %s %s:%d seq %d lvl %d (to %s %s) - %s",
                oam->name, stream, dach.session, dach.seq, dach.level,
                "request_get_return_ip(ping_req)", "request_get_return_port(ping_req)", j_msg);
        //oam_send_reply(request_get_return_ip(ping_req), request_get_return_port(ping_req), j_msg, msg_len);
        free(j_msg);
    }
    json_delete(j);
    delete_oam_request(ping_req);
    return false;
}

// @returns false on error
static bool process_rping_request(struct OamEndPoint *oam, struct Packet *p, struct JsonValue *j)
{
    int port=6634;
    char *reply_address=NULL;

    INTERPRET_DACH(p->buf + p->headers[1].start);

    if (!get_return_ip_port(j, &reply_address, &port)) {
        json_delete(j);
        return false;
    }

    struct JsonValue *cmd = json_object_get_string(j, "command");
    if (cmd == NULL) {
        //TODO reply error?
        json_delete(j);
        free(reply_address);
        return false;
    }

    struct OamRequest *ping_req = parse_ping_command(cmd->v.string, false, true, NULL);
    //request_set_return(ping_req, reply_address, port);
    if (request_get_error(ping_req) == NULL) {
        //TODO fix this to behave like rlist
        // if (strcmp(oam->stream, ping_req->mep_start->stream_name) != 0) {
            // ping_req->error = strdup("rping target point and ping start point are in different streams");
            // return send_rping_error(oam, p, j, ping_req);
        // }

        struct JsonValue *jstream = json_object_get_string(j, "stream");
        if (jstream == NULL) {
            request_set_error(ping_req, strdup("rping request contains no stream name"));
            return send_rping_error(oam, p, j, ping_req);
        }
        request_set_originator(ping_req, jstream->v.string, dach.session);

        if (!initiate_request(ping_req)) {
            request_set_error(ping_req, strdup_printf("ping request could not be sent: %s", request_get_error(ping_req)));
            return send_rping_error(oam, p, j, ping_req);
        }
    } else {
        return send_rping_error(oam, p, j, ping_req);
    }
    json_delete(j);
    return false;
}

// @returns false on error
static bool process_trigger_request(struct OamEndPoint *oam, struct Packet *p, struct JsonValue *j)
{
    INTERPRET_DACH(p->buf + p->headers[1].start);

    struct JsonValue *jseq = json_object_get_number(j, "seq");
    struct JsonValue *jtarget = json_object_get_string(j, "target");
    struct JsonValue *jstream = json_object_get_string(j, "stream");
    struct JsonValue *jsrc = json_object_get_string(j, "source");
    if ((jseq == NULL) || (jtarget == NULL) || (jstream == NULL) || (jsrc == NULL)) {
        log_error("OAM trigger packet does not have required fields");
        json_delete(j);
        return false;
    }

    struct JsonValue *js = json_object();
    json_object_insert(js, "seq", json_number(jseq->v.number));
    json_object_insert(js, "target", json_string(jtarget->v.string));
    json_object_insert(js, "stream", json_string(jstream->v.string));
    json_object_insert(js, "source", json_string(jsrc->v.string));

    json_object_insert(js, "level", json_number(dach.level));
    json_object_insert(js, "session", json_number(dach.session));
    json_object_insert(js, "node_id", json_number(dach.nodeid));

    struct MepStart *mep = find_mep_start(oam->name);
    struct JsonValue *jlist = mep_start_get_state_by_target(mep);
    json_object_insert(js, "mep", jlist);

    notification_push_event("triggered_receiver", NOTIF_INFO, js);

    json_delete(j);
    return false;
}

struct AddstartState {
    struct JsonValue *jlist;
    struct OamEndPoint *oam;
};
static int addstart_cb(const char *key, void *value, void *userdata)
{
    struct AddstartState *st = (struct AddstartState *)userdata;
    struct MepStart *mep = (struct MepStart *)value;

    if (same_compound_stream(mep->stream_name, st->oam->stream)) {
        //TODO supply more info: level, type
        json_array_push(st->jlist, json_string(key));
    }

    return 1;
}

// @returns false on error
static bool process_rlist_request(struct OamEndPoint *oam, struct Packet *p, struct JsonValue *j)
{
    int port=6634;
    char *reply_address=NULL;

    INTERPRET_DACH(p->buf + p->headers[1].start);

    if (!get_return_ip_port(j, &reply_address, &port)) {
        json_delete(j);
        return false;
    }

    json_object_remove(j, "return");
    json_object_insert(j, "code", json_string("reply"));
    json_object_insert(j, "sequence", json_number(dach.seq));
    json_object_insert(j, "level", json_number(dach.level));
    json_object_insert(j, "nodeid", json_number(dach.nodeid));
    json_object_insert(j, "session", json_number(dach.session));
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
    foreach_mep_start(addstart_cb, &st);
    json_object_insert(j, "list", jlist);

    unsigned msg_len=0;
    char *j_msg = json_serialize(j, &msg_len);
    if (j_msg) {
        log_packet("send rlist reply %s:%d seq %d lvl %d (to %s %d) - %s",
                stream, dach.session, dach.seq, dach.level,
                reply_address, port, j_msg);

        oam_send_reply(reply_address, port, j_msg, msg_len);
        free(j_msg);
    }
    free(reply_address);
    json_delete(j);
    return j_msg != NULL;
}

// Only pre-Elimination AutoMIPs react to mask/unmask
// pre-elim MIPs update the last heartbeat timestamp
// @returns false on error
static bool process_mask_request(struct OamEndPoint *oam, struct Packet *p, struct JsonValue *j)
{
    (void) p;
    // not auto-generated MIPs ignore the mask signals
    if (strncmp(oam->name, "o_", 2))
        return false;

    const char *req_type = json_object_get_string(j, "type")->v.string;
    struct MepStart *mep = find_mep_start(oam->name);
    if (mep && mep->target && mep->target->type == PO_SEQREC) {

        clock_gettime(CLOCK_REALTIME, &mep->last_mask_heartbeat);

        HASHMAP_ITERATE(mep->target->meps, it) {
            // we updated the elimination pre-MIP's heartbeat timestamp
            // now we can wake up the post-MIP's mask checker thread
            // to calculate the number of masked paths
            const char *key = hash_iterator_key(&it);
            if (strstr(key, "_post-")) {
                struct MepStart *postmep = find_mep_start(key);
                mep_start_wakeup_mask_checker(postmep);
                log_debug("%s: '%s' signal received", oam->name, req_type);
                return true;
            }
        }
    }
    return false;
}

static bool process_request(struct OamEndPoint *oam, struct Packet *p)
{
    // note: we made sure in conf_actions.c that at this point of the pipeline the packet starts with mpls+dcw

    // let's reinterpret the header structure if it wasn't already done by an earlier MIP
    if (p->headers[1].type != PROTO_ID_OAM) {
        for (unsigned i=1; i<p->header_count-1; i++) {
            if (p->headers[i+1].start != p->headers[i].start + p->headers[i].len) {
                log_error("OAM packet is not continuous in memory at header %u type %s",
                        i, protocol_type_from_id(p->headers[i].type));
                return false;
            }
        }

        unsigned plen = packet_length(p);
        p->headers[1].type = PROTO_ID_OAM;
        p->headers[1].len = 8; // length of oam
        p->headers[2].type = PROTO_ID_PAYLOAD;
        p->headers[2].start = p->headers[1].start + 8;
        p->headers[2].len = plen - 4 - 8; // length of mpls and oam
        p->header_count = 3;
    }

    INTERPRET_DACH(p->buf + p->headers[1].start);
    char *msg = (char *)(p->buf + p->headers[2].start);

    //log_packet("packet (%s) at [%s level %d], ttl %d nib_ver %x sequence %x channel %x node %x level %x session %x\njson: %s",
    //        protocol_type_from_id(p->headers[1].type), oam->name, oam->level, p->ttl, oam_hdr[0], seq, channel, nodeid, level, session, msg);

    char *jerror;
    struct JsonValue *j = json_parse(msg, strlen(msg), &jerror);
    if (j==NULL || j->type != JSON_OBJECT) {
        log_error("Invalid JSON string in incoming OAM packet: %s", jerror);
        free(jerror);
        return false;
    }

    struct JsonValue *jreqt = json_object_get_string(j, "type");
    if (jreqt==NULL) {
        log_error("OAM packet has no request type");
        json_delete(j);
        return false;
    }

    struct JsonValue *jreqc = json_object_get_string(j, "code");
    if (jreqc==NULL) {
        log_error("OAM packet has no request code");
        json_delete(j);
        return false;
    }
    if (strcmp(jreqc->v.string, "request") != 0) {
        log_error("OAM packet is not a request but '%s'", jreqc->v.string);
        json_delete(j);
        return false;
    }

    if(dach.level < oam->level){
        /*fprintf(stderr, "MIP %s level %d Warning: dropping lower level (level %d) OAM packet.\n",
          oam->name, oam->level, level);*/
        json_delete(j);
        return false;
    }
    if(dach.level > oam->level) {
        json_delete(j);
        return true;
    }

    struct JsonValue *target = json_object_get_string(j, "target");
    if (target == NULL) {
        log_error("OAM packet has no target");
        json_delete(j);
        return false;
    }

    log_packet("%s received request type %s target %s level %u",
            oam->name, jreqt->v.string, target->v.string, dach.level);

    if (strcmp(jreqt->v.string, "ping") == 0) {
        struct JsonValue *jrr = json_object_get_array(j, "rr");
        if (jrr != NULL) {
            //TODO supply more info: type, level, attached object
            json_array_push(jrr, json_string(oam->name));

            unsigned js_length;
            char *js_string = json_serialize(j, &js_length);
            if (js_string == NULL) {
                log_error("could not add entry to route record");
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
    } else if (strcmp(jreqt->v.string, "trigger") == 0) {
        if (p->ttl == 0) {
            return false;
        }
        if ((strcmp(target->v.string, "any") == 0) || strcmp(target->v.string, oam->name) == 0) {
            process_trigger_request(oam, p, j);
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
    } else if (strcmp(jreqt->v.string, "mask") == 0 || strcmp(jreqt->v.string, "unamask") == 0) {
        if (process_mask_request(oam, p, j)) {
            return false; // mask signal successfully processed, DROP the packet
        }
        return oam->stop ? false : true;
    } else {
        //TODO unknown message type
        return false;
    }
}

struct request_msg {
    struct OamEndPoint *oam;
    struct PipelineIterator *pi;
};
static void *request_thread_fn(void *arg)
{
    (void)arg;

    while (1) {
        struct request_msg *msg = (struct request_msg *)messagequeue_pop(request_q, -1);
        if (msg == NULL)
            return NULL;

        struct MepStart *mep = msg->oam->mep;
        if (mep) {
            //TODO __atomic_fetch_add
            mep->oam_packets_passed += 1;
            mep->oam_octets_passed += packet_length(msg->pi->packet);
        }
        if (process_request(msg->oam, msg->pi->packet)) {
            log_packet("%s forwarding request", msg->oam->name);
            msg->pi->pos += 1;
            pipe_iterator_run(msg->pi);
        } else {
            log_packet("%s dropping request", msg->oam->name);
            pipe_iteraror_cancel(msg->pi);
        }
        free(msg);
    }

    return NULL;
}

void oam_recv_request(struct OamEndPoint *oam, struct PipelineIterator *pi)
{
    struct request_msg *msg = calloc_struct(request_msg);
    msg->oam = oam;
    msg->pi = pi;
    messagequeue_push(request_q, msg);
}




static bool send_message_outofband(struct JsonValue *msg, struct JsonValue *address)
{
    struct JsonValue *ip = json_object_get_string(address, "ip");
    struct JsonValue *port = json_object_get_number(address, "port");
    struct JsonValue *dmac = json_object_get_string(address, "dmac");
    struct JsonValue *vlan = json_object_get_number(address, "vlan");

    unsigned msg_len;
    char *msg_str = json_serialize(msg, &msg_len);
    json_delete(msg);
    if (msg_str == NULL) {
        log_error("failed to serialize out-of-band message");
        return false;
    }

    if (ip && port) {
        //TODO this function needs a new name
        int err = oam_send_reply(ip->v.string, port->v.number, msg_str, msg_len);
        free(msg_str);
        log_packet("sent out-of-band message len %u to %s %g err %d", msg_len,
                ip->v.string, port->v.number, err);
        json_delete(address);
        return err == 0;
    } else if (dmac && vlan) {
        //TODO packet socket, construct eth, cvlan etc.
    } else if (dmac) {
        //TODO packet socket, construct eth etc.
    } else {
        log_error("can't send message out-of-band without proper address");
        return false;
    }
    return false; //TODO we shouldn't need this
}

#define THROW(msg, ...)                     \
    do {                                    \
        log_error(msg, ##__VA_ARGS__);      \
        json_delete(js);                    \
        return false;                       \
    } while (0)

#define JS_OBJECT_GET(_json, _key, _type)                           \
    struct JsonValue *_key = json_object_get_##_type(_json, #_key); \
    if (_key == NULL) {                                             \
        THROW("No " #_key " in OAM message.");                      \
    }

static bool logpacket_reply(struct OAM_MaintenancePoint *mp, struct JsonValue *js, struct JsonValue *return_dup,
        const char *type)
{
    if (!log_enabled(PACKET))
        return false;

    const char *stream = "<unknown>";
    struct JsonValue *jstream = json_object_get_string(js, "stream");
    if (stream != NULL) {
        stream = jstream->v.string;
    }
    JS_OBJECT_GET(js, session, number);
    JS_OBJECT_GET(js, seq, number);
    JS_OBJECT_GET(js, level, number);

    struct JsonValue *ip = json_object_get_string(return_dup, "ip");
    struct JsonValue *port = json_object_get_number(return_dup, "port");
    struct JsonValue *dmac = json_object_get_string(return_dup, "dmac");
    struct JsonValue *vlan = json_object_get_number(return_dup, "vlan");
    const char *addr = ip ? ip->v.string : dmac->v.string;
    unsigned num = port ? port->v.number : vlan->v.number;

    log_packet("%s send %s %s:%g seq %g lvl %g to %s %u",
            mp_get_name(mp), type, stream,
            session->v.number, seq->v.number, level->v.number,
            addr, num);
    return true;
}

//TODO there is a huge deduplication opportunity in the new_process_*_request() functions

static bool new_process_ping_request(struct OAM_MaintenancePoint *mp, struct JsonValue *js, struct timespec recv_time)
{
    JS_OBJECT_GET(js, code, string);
    if (strcmp(code->v.string, "request") != 0) {
        THROW("OAM ping is '%s' not request", code->v.string);
    }

    //JS_OBJECT_GET(js, return, object); XXX we can't do this because return is a keyword
    struct JsonValue *return_addr = json_object_get_object(js, "return");
    if (return_addr == NULL) {
        THROW("No return address in OAM ping request message.");
    }
    struct JsonValue *return_dup = json_duplicate(return_addr);
    json_object_remove(js, "return");

    //JS_OBJECT_GET(js, object, any); XXX we can't do this because any is not a type
    struct JsonValue *jos = json_object_get_any(js, "object");
    if (jos != NULL) {
        json_object_remove(js, "object");
        json_object_insert(js, "receiver", mp_get_state_json(mp, 1));
    } else {
        json_object_insert(js, "receiver", mp_get_state_json(mp, 0));
    }

    json_object_insert(js, "code", json_string("reply"));
    json_object_insert(js, "recv_s", json_number(recv_time.tv_sec));
    json_object_insert(js, "recv_ns", json_number(recv_time.tv_nsec));

    //TODO the old code also added the mpls label from the packet header, but we can't do that here

    logpacket_reply(mp, js, return_dup, "ping reply");

    return send_message_outofband(js, return_dup);
}

static bool new_send_rping_error(struct OAM_MaintenancePoint *mp, struct JsonValue *js, struct timespec recv_time,
        const char *error)
{
    struct JsonValue *return_addr = json_object_get_object(js, "return");
    struct JsonValue *return_dup = json_duplicate(return_addr);
    json_object_remove(js, "return");

    json_object_insert(js, "code", json_string("error"));
    json_object_insert(js, "recv_s", json_number(recv_time.tv_sec));
    json_object_insert(js, "recv_ns", json_number(recv_time.tv_nsec));
    json_object_insert(js, "receiver", json_string(mp_get_name(mp)));
    json_object_insert(js, "error", json_string(error));

    logpacket_reply(mp, js, return_dup, "rping error");

    return send_message_outofband(js, return_dup);
}

static bool new_process_rping_request(struct OAM_MaintenancePoint *mp, struct JsonValue *js, struct timespec recv_time)
{
    JS_OBJECT_GET(js, code, string);
    if (strcmp(code->v.string, "request") != 0) {
        THROW("OAM rping is '%s' not request", code->v.string);
    }

    //JS_OBJECT_GET(js, return, object); XXX we can't do this because return is a keyword
    struct JsonValue *return_addr = json_object_get_object(js, "return");
    if (return_addr == NULL) {
        THROW("No return address in OAM rping request message.");
    }

    JS_OBJECT_GET(js, stream, string);
    JS_OBJECT_GET(js, session, number);
    JS_OBJECT_GET(js, command, string);

    struct OamRequest *ping_req = parse_ping_command(command->v.string, false, true, NULL);
    if (request_get_error(ping_req) == NULL) {
        if (!same_compound_stream(request_get_stream_name(ping_req), mp_get_stream_name(mp))) {
            const char *error = strdup_printf("could not create ping request: streams '%s' and '%s' are not related",
                    request_get_stream_name(ping_req), mp_get_stream_name(mp));
            delete_oam_request(ping_req);
            return new_send_rping_error(mp, js, recv_time, error);
        }

        request_set_return_addr(ping_req, json_duplicate(return_addr));

        request_set_originator(ping_req, stream->v.string, session->v.number);

        if (!initiate_request(ping_req)) {
            const char *error = strdup_printf("could not send ping request: %s", request_get_error(ping_req));
            delete_oam_request(ping_req);
            return new_send_rping_error(mp, js, recv_time, error);
        }
    } else {
        const char *error = strdup_printf("could not create ping request: %s", request_get_error(ping_req));
        delete_oam_request(ping_req);
        return new_send_rping_error(mp, js, recv_time, error);
    }
    json_delete(js);
    return false;
}

struct AddMPState {
    struct JsonValue *jlist;
    const char *mp_stream;
};
static int add_mp_cb(const char *key, void *value, void *userdata)
{
    struct AddMPState *st = (struct AddMPState *)userdata;
    struct OAM_MaintenancePoint *mp = (struct OAM_MaintenancePoint *)value;

    if (same_compound_stream(mp_get_stream_name(mp), st->mp_stream))
        if (mp_can_send(mp))
            json_array_push(st->jlist, json_string(key));

    return 1;
}

static bool new_process_rlist_request(struct OAM_MaintenancePoint *mp, struct JsonValue *js, struct timespec recv_time)
{
    JS_OBJECT_GET(js, code, string);
    if (strcmp(code->v.string, "request") != 0) {
        THROW("OAM rlist is '%s' not request", code->v.string);
    }

    //JS_OBJECT_GET(js, return, object); XXX we can't do this because return is a keyword
    struct JsonValue *return_addr = json_object_get_object(js, "return");
    if (return_addr == NULL) {
        THROW("No return address in OAM rping request message.");
    }

    struct JsonValue *return_dup = json_duplicate(return_addr);
    json_object_remove(js, "return");

    json_object_insert(js, "code", json_string("reply"));
    json_object_insert(js, "recv_s", json_number(recv_time.tv_sec));
    json_object_insert(js, "recv_ns", json_number(recv_time.tv_nsec));
    json_object_insert(js, "receiver", mp_get_state_json(mp, 0));

    struct JsonValue *jlist = json_array();
    struct AddMPState st = { jlist, mp_get_stream_name(mp) };
    foreach_mp(true, add_mp_cb, &st);
    json_object_insert(js, "list", jlist);

    //TODO the old code also added the mpls label from the packet header, but we can't do that here

    logpacket_reply(mp, js, return_dup, "rlist reply");

    return send_message_outofband(js, return_dup);
}

static bool new_process_trigger_request(struct OAM_MaintenancePoint *mp, struct JsonValue *js, struct timespec recv_time)
{
    JS_OBJECT_GET(js, code, string);
    if (strcmp(code->v.string, "request") != 0) {
        THROW("OAM rlist is '%s' not request", code->v.string);
    }

    //TODO de-duplicate this with trigger_mep_push_notification() in oam_request.c
    JS_OBJECT_GET(js, seq, number);
    JS_OBJECT_GET(js, level, number);
    JS_OBJECT_GET(js, nodeid, number);
    JS_OBJECT_GET(js, session, number);
    JS_OBJECT_GET(js, source, string);
    JS_OBJECT_GET(js, stream, string);
    JS_OBJECT_GET(js, target, string);

    struct JsonValue *notif = json_object();
    json_object_insert(notif, "seq", json_number(seq->v.number));
    json_object_insert(notif, "level", json_number(level->v.number));
    json_object_insert(notif, "node_id", json_number(nodeid->v.number));
    json_object_insert(notif, "session", json_number(session->v.number));
    json_object_insert(notif, "recv_s", json_number(recv_time.tv_sec));
    json_object_insert(notif, "recv_ns", json_number(recv_time.tv_nsec));
    json_object_insert(notif, "source", json_string(source->v.string));
    json_object_insert(notif, "stream", json_string(stream->v.string));
    json_object_insert(notif, "target", json_string(target->v.string));

    struct JsonValue *jlist = mp_get_state_json_by_object(mp);
    json_object_insert(notif, "mp", jlist);

    notification_push_event("triggered_receiver", NOTIF_INFO, notif);
    return false;
}

static bool process_inband_message(struct OAM_MaintenancePoint *mp, struct JsonValue *js,
        unsigned ttl, struct timespec recv_time, bool *message_modified)
{
    JS_OBJECT_GET(js, level, number);
    int levelcmp = mp_compare_level(mp, level->v.number);
    if (levelcmp < 0) {
        json_delete(js);
        return false;
    }
    if (levelcmp > 0) {
        json_delete(js);
        return true;
    }

    JS_OBJECT_GET(js, type, string);
    JS_OBJECT_GET(js, code, string);
    JS_OBJECT_GET(js, target, string);

    log_packet("%s received OAM type '%s' code '%s' target '%s' level %f",
            mp_get_name(mp), type->v.string, code->v.string,
            target->v.string, level->v.number);

    if (strcmp(type->v.string, "ping") == 0) {
        struct JsonValue *jrr = json_object_get_array(js, "rr");
        if (jrr) {
            json_array_push(jrr, json_string(mp_get_name(mp)));
            *message_modified = true;
        }

        if (ttl == 0) {
            new_process_ping_request(mp, js, recv_time);
            return false;
        }
        if (strcmp(target->v.string, "any") == 0) {
            if (!new_process_ping_request(mp, js, recv_time))
                return false;
            return mp_get_type(mp) != OAM_Stop;
        }
        if (strcmp(target->v.string, mp_get_name(mp)) == 0) {
            new_process_ping_request(mp, js, recv_time);
            return false;
        }
        return mp_get_type(mp) != OAM_Stop;
    } else if (strcmp(type->v.string, "rping") == 0) {
        if (ttl == 0) {
            return false;
        }
        if (strcmp(target->v.string, "any") == 0) {
            return false;
        }
        if (strcmp(target->v.string, mp_get_name(mp)) == 0) {
            new_process_rping_request(mp, js, recv_time);
            return false;
        }
        return mp_get_type(mp) != OAM_Stop;
    } else if (strcmp(type->v.string, "rlist") == 0) {
        if (ttl == 0) {
            return false;
        }
        if (strcmp(target->v.string, "any") == 0) {
            if (!new_process_rlist_request(mp, js, recv_time))
                return false;
            return mp_get_type(mp) != OAM_Stop;
        }
        if (strcmp(target->v.string, mp_get_name(mp)) == 0) {
            new_process_rlist_request(mp, js, recv_time);
            return false;
        }
        return mp_get_type(mp) != OAM_Stop;
    } else if (strcmp(type->v.string, "trigger") == 0) {
        if (ttl == 0) {
            return false;
        }
        if (strcmp(target->v.string, "any") == 0) {
            if (!new_process_trigger_request(mp, js, recv_time))
                return false;
            return mp_get_type(mp) != OAM_Stop;
        }
        if (strcmp(target->v.string, mp_get_name(mp)) == 0) {
            new_process_trigger_request(mp, js, recv_time);
            return false;
        }
        return mp_get_type(mp) != OAM_Stop;
    } else if (strcmp(type->v.string, "mask") == 0) {
        //TODO figure out how process_mask_request() works (most likely it doesn't)
    } else if (strcmp(type->v.string, "unamask") == 0) { // compatibility with the old code :)
    } else {
        log_error("%s received unknown OAM type '%s'",
                mp_get_name(mp), type->v.string);
        return false;
    }
    return false; //TODO we shouldn't need this
}

#undef JS_OBJECT_GET
#undef THROW

struct inband_msg {
    struct OAM_MaintenancePoint *mp;
    struct PipelineIterator *pi;
};
static void *inband_receiver_th(void *arg)
{
    (void)arg;

    while (1) {
        struct inband_msg *msg = (struct inband_msg *)messagequeue_pop(inband_q, -1);
        if (msg == NULL)
            return NULL;

        mp_count_received_message(msg->mp, msg->pi->packet);

        if (mp_reinterpret_oam_packet(msg->mp, msg->pi->packet) == false) {
            log_error("packet structure is not valid for OAM");
            pipe_iteraror_cancel(msg->pi);
            continue;
        }

        struct JsonValue *js = mp_unpack_message(msg->mp, msg->pi->packet);
        if (js == NULL) {
            log_error("invalid JSON payload in received message");
            pipe_iteraror_cancel(msg->pi);
            continue;
        }

        bool message_modified = false;

        //TODO TSN has no ttl, so p->ttl=0 and we are in trouble
        if (process_inband_message(msg->mp, js, msg->pi->packet->ttl, msg->pi->packet->recv_time, &message_modified)) {
            if (message_modified) {
                if (!mp_update_message_payload(msg->mp, msg->pi->packet, js)) {
                    log_error("could not update JSON payload in received message");
                    pipe_iteraror_cancel(msg->pi);
                    continue;
                }
            }

            log_packet("%s forwarding message", mp_get_name(msg->mp));

            //TODO pipe_iterator_resume(msg->pi);
            msg->pi->pos += 1;
            pipe_iterator_run(msg->pi);
        } else {
            log_packet("%s dropping message", mp_get_name(msg->mp));
            pipe_iteraror_cancel(msg->pi);
        }

        free(msg);
    }

    return NULL;
}

void oam_receive_inband(struct OAM_MaintenancePoint *mp, struct PipelineIterator *pi)
{
    struct inband_msg *msg = calloc_struct(inband_msg);
    msg->mp = mp;
    msg->pi = pi;
    messagequeue_push(inband_q, msg);
}


struct outofband_msg {
    struct Interface *iface;
    const char *message;
};
static void *outofband_receiver_th(void *arg)
{
    (void)arg;

    while (1) {
        struct outofband_msg *msg = (struct outofband_msg *)messagequeue_pop(outofband_q, -1);
        if (msg == NULL)
            return NULL;

        process_reply(msg->message); //TODO do we need to adjust this?
        free(msg);
    }

    return NULL;
}

void oam_receive_outofband(struct Interface *iface, const char *message)
{
    struct outofband_msg *msg = calloc_struct(outofband_msg);
    msg->iface = iface;
    msg->message = strdup(message);
    messagequeue_push(outofband_q, msg);
}

void oam_count_packet(struct OamEndPoint *oam, struct Packet *p)
{
    struct MepStart *mep = find_mep_start(oam->name);
    if (mep)
        mep_start_count_passed(mep, p);
}

void init_message_module(bool have_reply_iface)
{
    request_q = new_messagequeue();
    request_thread = thread_launch(request_thread_fn, NULL, "oam request");

    if (have_reply_iface) {
        reply_q = new_messagequeue();
        reply_thread = thread_launch(reply_thread_fn, NULL, "oam reply");
    }

    inband_q = new_messagequeue();
    outofband_q = new_messagequeue();
    inband_receiver_thread = thread_launch(inband_receiver_th, NULL, "oam inband rcv");
    outofband_receiver_thread = thread_launch(outofband_receiver_th, NULL, "oam outband rcv");
}

void finish_message_module(void)
{
    messagequeue_push(request_q, NULL);
    if (reply_q)
        messagequeue_push(reply_q, NULL);
    thread_join(request_thread);
    thread_join(reply_thread);
    delete_messagequeue(request_q);
    delete_messagequeue(reply_q);

    messagequeue_push(inband_q, NULL);
    messagequeue_push(outofband_q, NULL);
    thread_join(inband_receiver_thread);
    thread_join(outofband_receiver_thread);
    delete_messagequeue(inband_q);
    delete_messagequeue(outofband_q);
}

// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#define OAM_INTERNAL

#include "oam_message.h"
#include "oam_command.h"
#include "oam_core.h"
#include "oam_maintenance.h"
#include "oam_session.h"

#include "if_udp_out.h"
#include "if_oam_eth.h"
#include "json.h"
#include "log.h"
#include "notification.h"
#include "oam.h"
#include "packet.h"
#include "pipeline.h"
#include "state.h"
#include "thread_utils.h"
#include "utils.h"
#include "inet_utils.h"

#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <arpa/inet.h>

DEFAULT_LOGGING_MODULE(OAM, INFO);

static struct Thread *inband_receiver_thread = NULL;
static struct MessageQueue *inband_q = NULL;
static struct Thread *outofband_receiver_thread = NULL;
static struct MessageQueue *outofband_q = NULL;


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
        char *stat_str = NULL;
        char *stat_str_log = NULL;
        struct JsonValue *o_info = json_object_get_object(jreceiver, "object");
        if (o_info) {
            JS_OBJECT_GET(jreceiver, data_packets, number);
            JS_OBJECT_GET(jreceiver, data_octets, number);
            JS_OBJECT_GET(jreceiver, oam_recv, number);
            JS_OBJECT_GET(jreceiver, oam_send, number);
            stat_str = strdup_printf("%s stats: data packets %.0f octets %.0f OAM recv %.0f sent %.0f\n\t", jreceivername->v.string,
                                    jreceiverdata_packets->v.number, jreceiverdata_octets->v.number, jreceiveroam_recv->v.number, jreceiveroam_send->v.number);
            stat_str_log = strdup_printf("%s stats: data packets %.0f octets %.0f OAM recv %.0f sent %.0f ; ", jreceivername->v.string,
                                    jreceiverdata_packets->v.number, jreceiverdata_octets->v.number, jreceiveroam_recv->v.number, jreceiveroam_send->v.number);

            obj_str = pipelineobject_sprintf_state_json(o_info, ", ", "\n\t\t");
            obj_str_log = pipelineobject_sprintf_state_json(o_info, ", ", "; ");
        }
        if (obj_str_log == NULL) obj_str_log = strdup("");
        if (stat_str_log == NULL) stat_str_log = strdup("");

        // Logging
        log_info("%s %s %s %s", reply_str, rr_str, stat_str_log, obj_str_log);
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
                strcat(reply_str, stat_str);
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


/*
 * Send OAM mesage out-of-band over UDP
 * Address: destination address string (can be either IPV4, IPv6)
 * Msg: pointer to the message
 * Return 0 on success
*/
static int send_udp_reply(const char *address, unsigned port, const char *msg, unsigned msg_len)
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

/*
 * Send OAM mesage out-of-band over ETH
 * Address: destination address string (MAC)
 * vlan: vlan id
 * Msg: pointer to the message
 * Return 0 on success
*/
static int send_eth_reply(const char *address, unsigned vid, const char *msg, unsigned msg_len, unsigned level)
{
    struct Interface *eth_oam_if = get_default_oam_eth_interface();
    if(!eth_oam_if) {
        log_warning("Can not send reply because no ETH_OAM if specified");
        return 1;
    }

    struct Packet *p = new_packet(NULL);
    packet_clear_headers(p);
    packet_enlarge_scratch(p);

    int h=0;
    packet_add_header(p, h, PROTO_ID_ETH, protocol_from_id(PROTO_ID_ETH)->bytelength);
    unsigned char *eth = p->buf + p->headers[0].start;
    ether_pton(address, eth);
    memset(eth+6, 0, 6);

    int if_vid = oam_eth_if_get_vlan(eth_oam_if);
    if((if_vid != -1) || (vid == 0)) {
        // VLAN iface, no need to add VLAN

        if( (vid != 0) && ((unsigned)if_vid != vid) )
            log_warning("Different VLAN ID specified for OAM_ETH VLAN return interface: %d. VLAN ignored.", vid);

        eth[12] = 0x89;             // OAM ethertype or VLAN ethertype
        eth[13] = 0x02;
    } else {
        eth[12] = 0x81;             // OAM ethertype or VLAN ethertype
        eth[13] = 0x00;

        // add VLAN header
        h++; packet_add_header(p, h, PROTO_ID_CVLAN, protocol_from_id(PROTO_ID_CVLAN)->bytelength); // cvlan or svlan
        unsigned char *vlan = p->buf + p->headers[h].start;
        vlan[0] = vid & 0xff;
        vlan[1] = (vid >> 8) & 0xff;
        vlan[2] = 0x89; // cfm
        vlan[3] = 0x02;
    }

    h++; packet_add_header(p, h, PROTO_ID_PAYLOAD, 4 + 3 + msg_len + 1);
    unsigned char *cfm = p->buf + p->headers[h].start;
    unsigned char *tlv = cfm + 4;
    cfm[0] = level << 5;
    cfm[1] = OAM_CFM_RESPONSE_OPCODE;
    cfm[2] = 0; // flags
    cfm[3] = 0; // tlv offset

    tlv[0] = 3; // generic data tlv
    tlv[1] = (msg_len >> 8) & 0xff;
    tlv[2] = msg_len & 0xff;
    tlv[3] = 0; // end tlv indicator
    memcpy(tlv+3, msg, msg_len+1); // also set the closing 0

    struct timespec sendtime;
    clock_gettime(CLOCK_REALTIME, &sendtime);
    p->recv_time = sendtime;
    timespec_to_tsntstamp(p->timestamp, &sendtime);

    eth_oam_if->send(eth_oam_if, p);
    delete_packet(p);

    return 0;
}

static bool send_message_outofband(struct OAM_MaintenancePoint *mp, const struct JsonValue *msg, struct JsonValue *address)
{
    struct JsonValue *ip = json_object_get_string(address, "ip");
    struct JsonValue *port = json_object_get_number(address, "port");
    struct JsonValue *dmac = json_object_get_string(address, "dmac");
    struct JsonValue *vlan = json_object_get_number(address, "vlan");

    unsigned msg_len;
    char *msg_str = json_serialize(msg, &msg_len);
    if (msg_str == NULL) {
        log_error("failed to serialize out-of-band message");
        return false;
    }

    if (ip && port) {
        int err = send_udp_reply(ip->v.string, port->v.number, msg_str, msg_len);
        free(msg_str);
        log_packet("sent UDP out-of-band message len %u to %s %g err %d", msg_len,
                ip->v.string, port->v.number, err);
        json_delete(address);
        return err == 0;
    } else if (dmac) {
        int err = send_eth_reply(dmac->v.string, vlan? vlan->v.number:0, msg_str, msg_len, mp_get_level(mp));
        free(msg_str);
        log_packet("sent ETH out-of-band message len %u to %s %g err %d", msg_len,
                dmac->v.string, vlan? vlan->v.number:0, err);
        json_delete(address);
        return err == 0;
    } else {
        log_error("can't send message out-of-band without proper address");
        return false;
    }
}

#define THROW(msg, ...)                     \
    do {                                    \
        log_error(msg, ##__VA_ARGS__);      \
        return false;                       \
    } while (0)

#define JS_OBJECT_GET(_json, _key, _type)                                   \
    struct JsonValue *_json##_key = json_object_get_##_type(_json, #_key);  \
    if (_json##_key == NULL) {                                              \
        THROW("No " #_key " in OAM message.");                              \
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
    if(!ip && !dmac) {  // avoid segfault when json is malformed
        log_error("neither ip nor dmac is present in ping request");
        return false;
    }
    const char *addr = ip ? ip->v.string : dmac->v.string;
    unsigned num = 0;
    if(ip && port) {
        num = port->v.number;
    }
    if(dmac && vlan){
        num = vlan->v.number;
    }

    log_packet("%s send %s %s:%g seq %g lvl %g to %s %u",
            mp_get_name(mp), type, stream,
            jssession->v.number, jsseq->v.number, jslevel->v.number,
            addr, num);
    return true;
}

//TODO there is a huge deduplication opportunity in the process_*_request() functions

static bool process_ping_request(struct OAM_MaintenancePoint *mp, struct JsonValue *js, struct timespec recv_time)
{
    JS_OBJECT_GET(js, code, string);
    if (strcmp(jscode->v.string, "request") != 0) {
        THROW("OAM ping is '%s' not request", jscode->v.string);
    }

    // we have to duplicate for the reply, because when we need to forward a
    // route-request we have to repackage @js into the request to be forwarded
    struct JsonValue *js_dup = json_duplicate(js);

    JS_OBJECT_GET(js_dup, return, object);
    struct JsonValue *return_dup = json_duplicate(js_dupreturn);
    json_object_remove(js_dup, "return");

    struct JsonValue *jos = json_object_get_any(js_dup, "object");
    json_object_remove(js_dup, "object");
    json_object_insert(js_dup, "receiver", mp_get_state_json(mp, jos != NULL));

    json_object_insert(js_dup, "code", json_string("reply"));
    json_object_insert(js_dup, "recv_s", json_number(recv_time.tv_sec));
    json_object_insert(js_dup, "recv_ns", json_number(recv_time.tv_nsec));

    //TODO the old code also added the mpls label from the packet header, but we can't do that here

    logpacket_reply(mp, js_dup, return_dup, "ping reply");

    bool ret = send_message_outofband(mp, js_dup, return_dup);
    json_delete(js_dup);
    return ret;
}

static bool send_rping_error(struct OAM_MaintenancePoint *mp, struct JsonValue *js, struct timespec recv_time,
        const char *error)
{
    JS_OBJECT_GET(js, return, object);
    struct JsonValue *return_dup = json_duplicate(jsreturn);
    json_object_remove(js, "return");

    json_object_insert(js, "code", json_string("error"));
    json_object_insert(js, "recv_s", json_number(recv_time.tv_sec));
    json_object_insert(js, "recv_ns", json_number(recv_time.tv_nsec));
    json_object_insert(js, "receiver", mp_get_state_json(mp, false));
    json_object_insert(js, "error", json_string(error));

    logpacket_reply(mp, js, return_dup, "rping error");

    return send_message_outofband(mp, js, return_dup);
}

static bool process_rping_request(struct OAM_MaintenancePoint *mp, struct JsonValue *js, struct timespec recv_time)
{
    JS_OBJECT_GET(js, code, string);
    if (strcmp(jscode->v.string, "request") != 0) {
        THROW("OAM rping is '%s' not request", jscode->v.string);
    }

    JS_OBJECT_GET(js, return, object);
    JS_OBJECT_GET(js, stream, string);
    JS_OBJECT_GET(js, session, number);
    JS_OBJECT_GET(js, command, string);

    struct OamRequest *ping_req = parse_ping_command(jscommand->v.string, false, true);
    if (request_get_error(ping_req) == NULL) {
        if (!same_compound_stream(request_get_stream_name(ping_req), mp_get_stream_name(mp))) {
            const char *error = strdup_printf("could not create ping request: streams '%s' and '%s' are not related",
                    request_get_stream_name(ping_req), mp_get_stream_name(mp));
            delete_oam_request(ping_req);
            return send_rping_error(mp, js, recv_time, error);
        }

        request_set_return_addr(ping_req, json_duplicate(jsreturn));

        request_set_originator(ping_req, jsstream->v.string, jssession->v.number);

        if (!initiate_request(ping_req, NULL)) {
            const char *error = strdup_printf("could not send ping request: %s", request_get_error(ping_req));
            delete_oam_request(ping_req);
            return send_rping_error(mp, js, recv_time, error);
        }
    } else {
        const char *error = strdup_printf("could not create ping request: %s", request_get_error(ping_req));
        delete_oam_request(ping_req);
        return send_rping_error(mp, js, recv_time, error);
    }
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

static bool process_rlist_request(struct OAM_MaintenancePoint *mp, struct JsonValue *js, struct timespec recv_time)
{
    JS_OBJECT_GET(js, code, string);
    if (strcmp(jscode->v.string, "request") != 0) {
        THROW("OAM rlist is '%s' not request", jscode->v.string);
    }

    JS_OBJECT_GET(js, return, object);
    struct JsonValue *return_dup = json_duplicate(jsreturn);
    json_object_remove(js, "return");

    json_object_insert(js, "code", json_string("reply"));
    json_object_insert(js, "recv_s", json_number(recv_time.tv_sec));
    json_object_insert(js, "recv_ns", json_number(recv_time.tv_nsec));
    json_object_insert(js, "receiver", mp_get_state_json(mp, false));

    struct JsonValue *jlist = json_array();
    struct AddMPState st = { jlist, mp_get_stream_name(mp) };
    foreach_mp(true, add_mp_cb, &st);
    json_object_insert(js, "list", jlist);

    //TODO the old code also added the mpls label from the packet header, but we can't do that here

    logpacket_reply(mp, js, return_dup, "rlist reply");

    return send_message_outofband(mp, js, return_dup);
}

static bool process_trigger_request(struct OAM_MaintenancePoint *mp, struct JsonValue *js, struct timespec recv_time)
{
    JS_OBJECT_GET(js, code, string);
    if (strcmp(jscode->v.string, "request") != 0) {
        THROW("OAM rlist is '%s' not request", jscode->v.string);
    }

    //TODO de-duplicate this with trigger_mep_push_notification() in oam_request.c
    JS_OBJECT_GET(js, seq, number);
    JS_OBJECT_GET(js, level, number);
    JS_OBJECT_GET(js, nodeid, number);
    JS_OBJECT_GET(js, session, number);
    JS_OBJECT_GET(js, source, object);
    JS_OBJECT_GET(js, stream, string);
    JS_OBJECT_GET(js, target, string);

    struct JsonValue *notif = json_object();
    json_object_insert(notif, "seq", json_number(jsseq->v.number));
    json_object_insert(notif, "level", json_number(jslevel->v.number));
    json_object_insert(notif, "node_id", json_number(jsnodeid->v.number));
    json_object_insert(notif, "session", json_number(jssession->v.number));
    json_object_insert(notif, "recv_s", json_number(recv_time.tv_sec));
    json_object_insert(notif, "recv_ns", json_number(recv_time.tv_nsec));
    json_object_insert(notif, "source", json_duplicate(jssource));
    json_object_insert(notif, "stream", json_string(jsstream->v.string));
    json_object_insert(notif, "target", json_string(jstarget->v.string));

    struct JsonValue *jlist = mp_get_state_json_by_object(mp);
    json_object_insert(notif, "mp", jlist);

    notification_push_event("triggered_receiver", NOTIF_INFO, notif);
    return false;
}

static bool process_inband_message(struct OAM_MaintenancePoint *mp, struct JsonValue *js,
        unsigned ttl, struct timespec recv_time, bool *message_modified)
{
    JS_OBJECT_GET(js, type, string);
    JS_OBJECT_GET(js, code, string);
    JS_OBJECT_GET(js, target, string);
    JS_OBJECT_GET(js, level, number);

    log_packet("%s received OAM type '%s' code '%s' target '%s' level %.0f",
            mp_get_name(mp), jstype->v.string, jscode->v.string,
            jstarget->v.string, jslevel->v.number);

    if (strcmp(jstype->v.string, "ping") == 0) {
        struct JsonValue *jrr = json_object_get_array(js, "rr");
        if (jrr) {
            json_array_push(jrr, json_string(mp_get_name(mp)));
            *message_modified = true;
        }

        if (ttl == 0) {
            process_ping_request(mp, js, recv_time);
            return false;
        }
        if (strcmp(jstarget->v.string, "any") == 0) {
            if (!process_ping_request(mp, js, recv_time))
                return false;
            return mp_get_type(mp) != OAM_Stop;
        }
        if (strcmp(jstarget->v.string, mp_get_name(mp)) == 0) {
            process_ping_request(mp, js, recv_time);
            return false;
        }
        return mp_get_type(mp) != OAM_Stop;
    } else if (strcmp(jstype->v.string, "rping") == 0) {
        if (ttl == 0) {
            return false;
        }
        if (strcmp(jstarget->v.string, "any") == 0) {
            return false;
        }
        if (strcmp(jstarget->v.string, mp_get_name(mp)) == 0) {
            process_rping_request(mp, js, recv_time);
            return false;
        }
        return mp_get_type(mp) != OAM_Stop;
    } else if (strcmp(jstype->v.string, "rlist") == 0) {
        if (ttl == 0) {
            return false;
        }
        if (strcmp(jstarget->v.string, "any") == 0) {
            if (!process_rlist_request(mp, js, recv_time))
                return false;
            return mp_get_type(mp) != OAM_Stop;
        }
        if (strcmp(jstarget->v.string, mp_get_name(mp)) == 0) {
            process_rlist_request(mp, js, recv_time);
            return false;
        }
        return mp_get_type(mp) != OAM_Stop;
    } else if (strcmp(jstype->v.string, "trigger") == 0) {
        if (ttl == 0) {
            return false;
        }
        if (strcmp(jstarget->v.string, "any") == 0) {
            if (!process_trigger_request(mp, js, recv_time))
                return false;
            return mp_get_type(mp) != OAM_Stop;
        }
        if (strcmp(jstarget->v.string, mp_get_name(mp)) == 0) {
            process_trigger_request(mp, js, recv_time);
            return false;
        }
        return mp_get_type(mp) != OAM_Stop;
    } else if (strcmp(jstype->v.string, "mask") == 0) {
        mp_receive_mask_signal(mp);
        return false;
    } else if (strcmp(jstype->v.string, "unmask") == 0) {
        mp_receive_unmask_signal(mp);
        return false;
    } else {
        log_error("%s received unknown OAM type '%s'",
                mp_get_name(mp), jstype->v.string);
        return false;
    }
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

        int levelcmp = mp_compare_level(msg->mp, msg->pi->packet);
        if (levelcmp < 0) {
            log_packet("%s dropping in-band message due to level", mp_get_name(msg->mp));
            pipe_iteraror_cancel(msg->pi);
            free(msg);
            continue;
        }
        if (levelcmp > 0) {
            log_packet("%s forwarding in-band message due to level", mp_get_name(msg->mp));
            //TODO pipe_iterator_resume(msg->pi);
            msg->pi->pos += 1;
            pipe_iterator_run(msg->pi);
            free(msg);
            continue;
        }

        struct JsonValue *js = mp_unpack_message(msg->mp, msg->pi->packet);
        if (js == NULL) {
            log_error("invalid JSON payload in received message");
            pipe_iteraror_cancel(msg->pi);
            free(msg);
            continue;
        }

        bool message_modified = false;
        unsigned char ttl = mp_get_ttl(msg->mp, msg->pi->packet);

        if (process_inband_message(msg->mp, js, ttl, msg->pi->packet->recv_time, &message_modified)) {
            if (message_modified) {
                if (!mp_pack_message_payload(msg->mp, msg->pi->packet, js)) {
                    log_error("could not update JSON payload in received message");
                    pipe_iteraror_cancel(msg->pi);
                    json_delete(js);
                    continue;
                }
            }

            log_packet("%s forwarding in-band message", mp_get_name(msg->mp));
            //TODO pipe_iterator_resume(msg->pi);
            msg->pi->pos += 1;
            pipe_iterator_run(msg->pi);
        } else {
            log_packet("%s dropping in-band message", mp_get_name(msg->mp));
            pipe_iteraror_cancel(msg->pi);
        }

        json_delete(js);
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
    char *message;
};
static void *outofband_receiver_th(void *arg)
{
    (void)arg;

    while (1) {
        struct outofband_msg *msg = (struct outofband_msg *)messagequeue_pop(outofband_q, -1);
        if (msg == NULL)
            return NULL;

        process_reply(msg->message);
        free(msg->message);
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

void init_message_module(void)
{
    inband_q = new_messagequeue();
    outofband_q = new_messagequeue();
    inband_receiver_thread = thread_launch(inband_receiver_th, NULL, "oam inband rcv");
    outofband_receiver_thread = thread_launch(outofband_receiver_th, NULL, "oam outband rcv");
}

void finish_message_module(void)
{
    messagequeue_push(inband_q, NULL);
    messagequeue_push(outofband_q, NULL);
    thread_join(inband_receiver_thread);
    thread_join(outofband_receiver_thread);
    delete_messagequeue(inband_q);
    delete_messagequeue(outofband_q);
}

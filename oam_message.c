// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#define OAM_INTERNAL

#include "oam.h"
#include "oam_message.h"
#include "oam_command.h"
#include "oam_core.h"
#include "oam_request.h"

#include "if_udp_out.h"
#include "json.h"
#include "log.h"
#include "object.h"
#include "packet.h"
#include "pipeline.h"
#include "state.h"
#include "time_utils.h"
#include "thread_utils.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>


DEFAULT_LOGGING_MODULE(OAM, INFO);

struct SessionTracker {
    char *conn_name; // NULL if not issued from a command connection
    time_t access_time;
    unsigned interval_ms;
    struct Thread *multireq_thread;
    struct oam_request *req; // needed to list the active request sessions
    bool live;
};

struct StreamSessions {
    struct SessionTracker sessions[16];
    unsigned last_session; // last request session on this stream TODO should be local to command thread
};

static struct HashMap *session_ids = NULL; // stream_name -> struct StreamSessions
static pthread_mutex_t session_lock; // should be used to protect the whole session_ids hash


static struct Thread *request_thread = NULL;
static struct MessageQueue *request_q = NULL;
static struct Thread *reply_thread = NULL;
static struct MessageQueue *reply_q = NULL;



struct StreamSessions *get_stream_sessions(const char *stream_name)
{
    pthread_mutex_lock(&session_lock);
    struct StreamSessions *stream = (struct StreamSessions *)hashmap_find(session_ids, stream_name);
    if (stream == NULL) {
        stream = calloc_struct(StreamSessions);
        hashmap_insert(session_ids, strdup(stream_name), stream);
    }
    pthread_mutex_unlock(&session_lock);
    return stream;
}

bool known_stream(const char *stream_name)
{
    pthread_mutex_lock(&session_lock);
    int contains = hashmap_contains(session_ids, stream_name);
    pthread_mutex_unlock(&session_lock);
    return contains ? true: false;
}

int alloc_session_id(struct StreamSessions *stream, struct oam_request *req,
        const char *conn_name, unsigned interval_ms)
{
    pthread_mutex_lock(&session_lock);

    unsigned next_id = (stream->last_session + 1) % 16;
    unsigned id = next_id;
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

    while (stream->sessions[id].live) {
        unsigned timeout = MAX(ceil(1.0 + 0.001*stream->sessions[id].interval_ms), 2);
        if (now.tv_sec > stream->sessions[id].access_time + timeout) {
            //log_info("session %u timeouted", id);
            stream->sessions[id].live = false;
            stream->sessions[id].multireq_thread = thread_stop(stream->sessions[id].multireq_thread);
            free(stream->sessions[id].conn_name);
            stream->sessions[id].conn_name = NULL;
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
        stream->sessions[id].live = true;
        stream->sessions[id].access_time = now.tv_sec + 1;
        stream->sessions[id].req = req;
        stream->sessions[id].multireq_thread = NULL;
        stream->sessions[id].interval_ms = interval_ms;
        stream->sessions[id].conn_name = conn_name ? strdup(conn_name) : NULL;
        pthread_mutex_unlock(&session_lock);
        return id;
    }
}

int stream_live_session_count(const struct StreamSessions *stream)
{
    pthread_mutex_lock(&session_lock);
    int live_session_count = 0;
    for (int i=0; i<16; i++) if (stream->sessions[i].live) live_session_count++;
    pthread_mutex_unlock(&session_lock);
    return live_session_count;
}

static void stream_stop_session(struct StreamSessions *stream, int session, struct command_connection *conn)
{
    FILE *cmd_w = command_connection_get_w(conn);
    if(stream->sessions[session].live){
        if (stream->sessions[session].multireq_thread) {
            stream->sessions[session].multireq_thread = thread_stop(stream->sessions[session].multireq_thread);
            free(stream->sessions[session].conn_name);
            stream->sessions[session].conn_name = NULL;
            fprintf(cmd_w,"stopped.");
        } else {
            fprintf(cmd_w,"not running.");
        }
        stream->sessions[session].live = false;
    } else {
        fprintf(cmd_w,"not live.");
    }
    command_connection_release_w(conn);
}

void stop_session(const char *stream_name, int session, struct command_connection *conn)
{
    pthread_mutex_lock(&session_lock);
    struct StreamSessions *stream = get_stream_sessions(stream_name);
    if (session==-1)
        session = stream->last_session;
    FILE *cmd_w = command_connection_get_w(conn);
    if (cmd_w) fprintf(cmd_w, "Stopping stream:session %s:%d - ", stream_name, session);
    stream_stop_session(stream, session, conn);
    if (cmd_w) fprintf(cmd_w, "\n");
    command_connection_release_w(conn);
    pthread_mutex_unlock(&session_lock);
}

static int stop_sessions_cb(const char *key, void *value, void *userdata)
{
    struct StreamSessions *stream = (struct StreamSessions *)value;
    struct command_connection *conn = (struct command_connection *)userdata;
    FILE *cmd_w = command_connection_get_w(conn);

    for (int i=0; i<16; i++) {
        if (stream->sessions[i].live == false) continue;
        if (command_connection_is_same(conn, stream->sessions[i].conn_name)) {
            if (cmd_w) fprintf(cmd_w, "Stopping stream:session %s:%d - ", key, i);
            stream_stop_session(stream, i, conn);
            if (cmd_w) fprintf(cmd_w, "\n");
        }
    }
    command_connection_release_w(conn);
    return 1;
}

void stop_all_sessions_of_connection(struct command_connection *conn)
{
    pthread_mutex_lock(&session_lock);
    hashmap_foreach(session_ids, stop_sessions_cb, conn);
    pthread_mutex_unlock(&session_lock);
}

int list_sessions_of_stream(struct StreamSessions *stream, FILE *cmd_w)
{
    bool has_sessions = false;
    for(int i=0; i<16; i++){
        if(stream->sessions[i].live){
            has_sessions = true;
        }
    }
    if (!has_sessions) return 1;

    for(int i=0; i<16; i++){
        if(stream->sessions[i].live){
            struct oam_request *req = stream->sessions[i].req;
            fprintf(cmd_w,"\t%d\t %s %s -> %s level %d\n",
                    i, request_get_type(req), request_get_start_name(req),
                    request_get_stop_name(req), request_get_level(req));
        }
    }
    return 1;
}

static int list_all_sessions_cb(const char *key, void *value, void *userdata)
{
    struct StreamSessions *stream = (struct StreamSessions *)value;
    FILE *cmd_w = (FILE *)userdata;
    fprintf(cmd_w, "Stream %s sessions:\n", key);
    return list_sessions_of_stream(stream, cmd_w);
}

int list_sessions_of_all_streams(FILE *cmd_w)
{
    int ret = hashmap_foreach_sorted(session_ids, list_all_sessions_cb, cmd_w);
    return ret;
}

void session_set_thread(struct StreamSessions *stream, int session, struct Thread *th)
{
    stream->sessions[session].multireq_thread = th;
}

struct Thread *session_get_thread(struct StreamSessions *stream, int session)
{
    return stream->sessions[session].multireq_thread;
}

void session_touch(struct StreamSessions *stream, int session)
{
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    stream->sessions[session].access_time = now.tv_sec + 1;
}


static int process_reply(const char *msg)
{
#define JS_OBJECT_GET(_key, _type, _json)                           \
    struct JsonValue *_key = json_object_get_##_type(_json, #_key); \
    if (_key == NULL) {                                             \
        log_error("No " #_key " in reply.");                        \
        json_delete(j);                                             \
        return -1;                                                  \
    }

    char reply_str[1400], rr_str[512];
    struct JsonValue *j = json_parse(msg, strlen(msg));
    if (j == NULL || j->type != JSON_OBJECT) {
        log_error("JSON in reply is invalid.");
        return -1;
    }

    JS_OBJECT_GET(type, string, j);

    if (strcmp(type->v.string, "newaddress") == 0) {
        JS_OBJECT_GET(code, string, j);
        if (strcmp(code->v.string, "notify") != 0) {
            log_error("newaddress message is '%s' instead of 'notify'", code->v.string);
            json_delete(j);
            return -1;
        }

        JS_OBJECT_GET(sendiface, string, j);
        struct Interface *sendif = state_get_interface(sendiface->v.string);
        if (sendif == NULL) {
            log_error("got newaddress notification for non-existing interface '%s'", sendiface->v.string);
            json_delete(j);
            return -1;
        }
        if (sendif->type != IF_UDP_OUT) {
            log_error("got newaddress notification for interface '%s' that is %s",
                    sendiface->v.string, iface_type_str(sendif->type));
            json_delete(j);
            return -1;
        }

        JS_OBJECT_GET(address, object, j);
        JS_OBJECT_GET(ip, string, address);
        JS_OBJECT_GET(port, number, address);
        log_debug("newaddress notification for %s is %s %.0f",
                sendiface->v.string, ip->v.string, port->v.number);
        udp_out_set_dst(sendif, ip->v.string, port->v.number);
        json_delete(j);
        return 0;
    }

    JS_OBJECT_GET(nodeid, number, j);
    JS_OBJECT_GET(target, string, j);
    JS_OBJECT_GET(sequence, number, j);
    JS_OBJECT_GET(level, number, j);
    JS_OBJECT_GET(receiver, string, j);
    JS_OBJECT_GET(stream, string, j);
    JS_OBJECT_GET(session, number, j);

    struct SessionTracker *sess = NULL;
    if (session->v.number < 0 || session->v.number > 15) {
        log_error("session id %.0f in reply is invalid", session->v.number);
        json_delete(j);
        return -1;
    } else {
        if (known_stream(stream->v.string)) {
            struct StreamSessions *ss = get_stream_sessions(stream->v.string);
            sess = &ss->sessions[(int)(session->v.number)];
            if (!sess->live) {
                log_error("Reply for non-live session %.0f of stream '%s'.", session->v.number, stream->v.string);
                sess = NULL;
            }
        } else {
            // TODO this happens on a central reply collector node -> not error
            log_error("Unknown stream name '%s' in reply.", stream->v.string);
        }
    }

    log_packet("recv reply %s:%.0f seq %.0f lvl %.0f - %s",
            stream->v.string, session->v.number, sequence->v.number, level->v.number, msg);

    struct command_connection *conn = NULL;
    if (sess) conn = find_command_connection(sess->conn_name);

    if (strcmp(type->v.string, "rlist") == 0) {
        JS_OBJECT_GET(code, string, j);
        if (strcmp(code->v.string, "reply") != 0) {
            log_error("rlist result is not a reply.");
            json_delete(j);
            return -1;
        }

        JS_OBJECT_GET(list, array, j);
        sprintf(reply_str, "Rlist result from %s:\n", receiver->v.string);
        for (unsigned i=0; i<json_array_size(list); i++) {
            struct JsonValue *str = json_array_at(list, i);
            if (str->type != JSON_STRING) {
                log_error("rlist result is not string.");
                json_delete(j);
                return -1;
            }
            strcat(reply_str, str->v.string);
            strcat(reply_str, "\n");
        }
        json_delete(j);
        if (conn) {
            FILE *cmd_w = command_connection_get_w(conn);
            if (cmd_w) fprintf(cmd_w, "%s\n", reply_str);
            command_connection_release_w(conn);
        }
    }
    else if (strcmp(type->v.string, "rping") == 0) {
        JS_OBJECT_GET(code, string, j);
        if (strcmp(code->v.string, "error") != 0) {
            log_error("rping response is not error.");
            json_delete(j);
            return -1;
        }

        JS_OBJECT_GET(error, string, j);
        snprintf(reply_str, sizeof(reply_str), "Rping error from %s : %s\n", receiver->v.string, error->v.string);
        json_delete(j);
        if (conn) {
            FILE *cmd_w = command_connection_get_w(conn);
            if (cmd_w) fprintf(cmd_w, "%s\n", reply_str);
            command_connection_release_w(conn);
        }
    }
    else if (strcmp(type->v.string, "ping") == 0) {
        JS_OBJECT_GET(code, string, j);
        if (strcmp(code->v.string, "reply") != 0) {
            log_error("ping result is not a reply.");
            json_delete(j);
            return -1;
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
                    type->v.string, stream->v.string, target->v.string, receiver->v.string, delay_diff.tv_sec, delay_diff.tv_nsec);
        }
        else
            sprintf(reply_str,"  oam_r %s:%.0f seq %.0f lvl %.0f R - %s on stream %s target %s; reply from %s",
                    stream->v.string, session->v.number, sequence->v.number, level->v.number,
                    type->v.string, stream->v.string, target->v.string, receiver->v.string);

        // Recorded route is single line, no difference between log/dump
        struct JsonValue *jrr = json_object_get_array(j, "rr");
        rr_str[0] = 0;
        if(jrr){
            sprintf(rr_str, "Record Route: [");
            for (unsigned i=0; i<json_array_size(jrr); i++) {
                struct JsonValue *rritem = json_array_at(jrr, i);
                if (rritem->type != JSON_STRING) {
                    log_error("record route item is not string");
                    json_delete(j);
                    return -1;
                }
                strcat(rr_str, " ");
                strcat(rr_str, rritem->v.string);
            }
            strcat(rr_str, " ]");
        }

        // different format between log/dump
        char *obj_str = NULL;
        char *obj_str_log = NULL;
        struct JsonValue *jos = json_object_get_object(j, "object");
        if (jos && jos->type == JSON_OBJECT){
            obj_str = sprintf_state_json(jos, ", ", "\n\t\t");
            obj_str_log = sprintf_state_json(jos, ", ", "; ");
        }
        if (obj_str_log == NULL) obj_str_log = strdup("");

        // Logging
        log_info("%s %s %s", reply_str, rr_str, obj_str_log);
        free(obj_str_log);

        json_delete(j);

        // check if we need to print to a telnet session
        if (conn == NULL) return 0; // this is a background ping

        FILE *cmd_w = command_connection_get_w(conn);
        if(command_connection_get_format(conn) == TF_JSON){
            if (cmd_w) fprintf(cmd_w, "%s\n", msg);
        } else {                                               // DUMP mode
            if (rr_str[0]) {
                strcat(reply_str, "\n\t");
                strcat(reply_str, rr_str);
            }
            if (obj_str) {
                strcat(reply_str, "\n\t");
                strcat(reply_str, obj_str);
                free(obj_str);
            }
            if (cmd_w) fprintf(cmd_w, "%s\n", reply_str);
        }
        command_connection_release_w(conn);

    }
    else {
        log_error("invalid reply type '%s'", type->v.string);
        json_delete(j);
        return -1;
    }
    return 0;
    #undef JS_OBJECT_GET
}

static void *reply_thread_fn(void *arg)
{
    (void)arg;

    while (1) {
        char *msg = (char *)messagequeue_pop(reply_q, -1);
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
        if (oam->target) {
            struct JsonValue *objinfo = oam->target->get_state(oam->target);
            json_object_insert(j, "object", objinfo);
        }
    }

    json_object_remove(j, "return");
    json_object_insert(j, "code", json_string("reply"));
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
    if (j_msg) {
        log_packet("send ping reply %s %s:%d seq %d lvl %d (to %s %d) - %s", oam->name, stream, session, seq, level,
                reply_address, port, j_msg);

        oam_send_reply(reply_address, port, j_msg, msg_len);
        free(j_msg);
    }
    free(reply_address);
    json_delete(j);
    return j_msg != NULL;
}

static bool send_rping_error(struct OamEndPoint *oam, struct Packet *p, struct JsonValue *j,
        struct oam_request *ping_req)
{
    unsigned char *oam_hdr = p->buf + p->headers[1].start;
    unsigned char seq = oam_hdr[1];
    //unsigned short channel = (oam_hdr[2]<<8)+oam_hdr[3];
    unsigned short nodeid = (oam_hdr[4]<<8)+oam_hdr[5];
    unsigned char level = oam_hdr[6] >> 1;
    unsigned char session = oam_hdr[7] & 0x0f;

    json_object_remove(j, "return");
    json_object_insert(j, "code", json_string("error"));
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

    json_object_insert(j, "error", json_string(request_get_error(ping_req)));

    unsigned msg_len=0;
    char *j_msg = json_serialize(j, &msg_len);
    if (j_msg) {
        log_packet("send rping error %s %s:%d seq %d lvl %d (to %s %d) - %s", oam->name, stream, session, seq, level,
                request_get_return_ip(ping_req), request_get_return_port(ping_req), j_msg);
        oam_send_reply(request_get_return_ip(ping_req), request_get_return_port(ping_req), j_msg, msg_len);
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
    if (cmd == NULL) {
        //TODO reply error?
        json_delete(j);
        free(reply_address);
        return false;
    }

    struct oam_request *ping_req = parse_ping_command(cmd->v.string, false, true, NULL);
    request_set_return(ping_req, reply_address, port);
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
        request_set_originator(ping_req, strdup(jstream->v.string), session);

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

struct AddstartState {
    struct JsonValue *jlist;
    struct OamEndPoint *oam;
};
static int addstart_cb(const char *key, void *value, void *userdata)
{
    struct AddstartState *st = (struct AddstartState *)userdata;
    struct MepStart *mep = (struct MepStart *)value;

    if (mep_start_in_stream(mep, st->oam->stream)) {
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
    json_object_insert(j, "code", json_string("reply"));
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
    foreach_mep_start(addstart_cb, &st);
    json_object_insert(j, "list", jlist);

    unsigned msg_len=0;
    char *j_msg = json_serialize(j, &msg_len);
    if (j_msg) {
        log_packet("send rlist reply %s:%d seq %d lvl %d (to %s %d) - %s", stream, session, seq, level,
                reply_address, port, j_msg);

        oam_send_reply(reply_address, port, j_msg, msg_len);
        free(j_msg);
    }
    free(reply_address);
    json_delete(j);
    return j_msg != NULL;
}

static bool process_request(struct OamEndPoint *oam, struct Packet *p)
{
    // note: we made sure in conf_actions.c that at this point of the pipeline the packet starts with mpls+dcw

    for (unsigned i=1; i<p->header_count-1; i++) {
        if (p->headers[i+1].start != p->headers[i].start + p->headers[i].len) {
            log_error("OAM packet is not continuous in memory at header %u type %s",
                    i, protocol_type_from_id(p->headers[i].type));
            return false;
        }
    }

    // let's reinterpret the header structure
    p->headers[1].type = PROTO_ID_OAM;
    p->headers[1].len = 8; // length of oam
    p->headers[2].type = PROTO_ID_PAYLOAD;
    p->headers[2].start = p->headers[1].start + 8;
    p->headers[2].len = p->len - 4 - 8; // length of mpls and oam
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
        log_error("Invalid JSON string in incoming OAM packet");
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
        log_error("OAM packet has no target");
        json_delete(j);
        return false;
    }

    log_packet("%s received request type %s target %s level %u", oam->name, jreqt->v.string, target->v.string, level);

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

struct request_msg {
    struct OamEndPoint *oam;
    struct PipelineIterator *pi;
};
static void *request_thread_fn(void *arg)
{
    (void)arg;

    while (1) {
        struct request_msg *msg = (struct request_msg *)messagequeue_pop(request_q, -1);
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

void init_msg_module(bool have_command_iface, bool have_reply_iface)
{
    request_q = new_messagequeue();
    request_thread = thread_launch(request_thread_fn, NULL, "oam request");

    if (have_command_iface) {
        pthread_mutex_init(&session_lock, NULL);
        session_ids = new_hashmap(11, NULL, NULL);
    }

    if (have_reply_iface) {
        reply_q = new_messagequeue();
        reply_thread = thread_launch(reply_thread_fn, NULL, "oam reply");
    }
}

void finish_msg_module(void)
{
    thread_stop(request_thread);
    thread_stop(reply_thread);
    delete_messagequeue(request_q);
    delete_messagequeue(reply_q);

    delete_hashmap(session_ids);
}


// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#define OAM_INTERNAL

#include "oam.h"
#include "oam_message.h"
#include "oam_command.h"
#include "oam_core.h"

#include "json.h"
#include "log.h"
#include "object.h"
#include "packet.h"
#include "pipeline.h"
#include "time_utils.h"
#include "thread_utils.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <unistd.h>
#include <pthread.h>
#include <netdb.h>


#define OAM_RCVY_RESET_MS 5000
#define OAM_PING_TTL 64
#define OAM_CHANNEL 1 /* Management Communication Channel (MCC), similar format to ours */


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
    struct StreamSessions *stream = hashmap_find(session_ids, stream_name);
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

static int alloc_session_id(struct StreamSessions *stream, struct oam_request *req)
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
        stream->sessions[id].conn_name = req->conn_name ? strdup(req->conn_name) : NULL;
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
    struct StreamSessions *stream = value;
    struct command_connection *conn = userdata;
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
                    i, req->type, req->mep_start->name, req->mep_stop, req->level);
        }
    }
    return 1;
}

static int list_all_sessions_cb(const char *key, void *value, void *userdata)
{
    struct StreamSessions *stream = value;
    FILE *cmd_w = userdata;
    fprintf(cmd_w, "Stream %s sessions:\n", key);
    return list_sessions_of_stream(stream, cmd_w);
}

int list_sessions_of_all_streams(FILE *cmd_w)
{
    int ret = hashmap_foreach_sorted(session_ids, list_all_sessions_cb, cmd_w);
    return ret;
}



struct oam_request *new_oam_request(const char *type, struct command_connection *conn)
{
    struct oam_request *req = calloc_struct(oam_request);

    req->conn_name = conn ? strdup(conn->name) : NULL;
    req->type = type;
    req->ttl = OAM_PING_TTL;
    req->count = 1;
    req->interval_ms = 1000;

    return req;
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
    struct StreamSessions *stream = get_stream_sessions(req->mep_start->stream_name);
    stream->sessions[req->session_id].interval_ms = req->interval_ms; //TODO why is this here?

    while(1){
        req->seq = seq & 0xFF;
        send_request(req);
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        stream->sessions[req->session_id].access_time = now.tv_sec + 1;
        seq++;
        if((req->count != 0) && (seq >= req->count)) break;
        usleep(req->interval_ms * 1000);
    }
    struct Thread *th = stream->sessions[req->session_id].multireq_thread;
    //TODO keep the session live until we receive the replies
    // stream->sessions[req->session_id].live = false;
    stream->sessions[req->session_id].multireq_thread = NULL;
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
        if (cmd_w) fprintf(cmd_w, "mep start '%s' has no pipeline!?!\n", req->mep_start->name);
        command_connection_release_w(conn);
        return false;
    }

    struct StreamSessions *stream = get_stream_sessions(req->mep_start->stream_name);
    int session_id = alloc_session_id(stream, req);
    if (session_id < 0) {
        if (cmd_w) fprintf(cmd_w, "stream %s has no free session id\n", req->mep_start->stream_name);
        command_connection_release_w(conn);
        return false;
    }

    req->session_id = session_id;
    req->seq = 0;

    log_info("request %s stream %s:%d seq %d lvl %d type %s mep %s -> %s"
            " count %d interval %d, rr: %s os: %s [reply to ip: %s, port: %u]",
             req->mep_start->mep_name, req->mep_start->stream_name, req->session_id,
             req->seq, req->level, req->type, req->mep_start->name, req->mep_stop, req->count, req->interval_ms,
             req->record_route?"yes":"no", req->object_state?"yes":"no", req->return_ip, req->return_port);

    if (cmd_w) fprintf(cmd_w, "OAM request %s session %u seq %u, %s -> %s level %d count %d interval %d,"
            " rr: %s os: %s\t[reply to ip: %s, port: %u]\n",
            req->type, req->session_id, req->seq, req->mep_start->name, req->mep_stop, req->level, req->count, req->interval_ms,
            req->record_route?"yes":"no", req->object_state?"yes":"no", req->return_ip, req->return_port);
    command_connection_release_w(conn);

    if(req->count == 1){
        stream->sessions[session_id].multireq_thread = NULL;
        send_request(req);
        delete_oam_request(req);
    } else {
        stream->sessions[session_id].multireq_thread = thread_launch(oam_request_thread, req, "oam req %d", session_id);
        if (stream->sessions[session_id].multireq_thread == NULL) {
            log_error("could not create new ping thread");
            return false;
        }
    }

    return true;
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
    JS_OBJECT_GET(nodeid, number, j);
    JS_OBJECT_GET(type, string, j);
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

    log_packet("oam recv reply %s:%.0f seq %.0f lvl %.0f D - %s",
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
        for (struct JsonArray *l = list->v.array; l; l=l->next) {
            if (l->val->type != JSON_STRING) {
                log_error("rlist result is not string.");
                json_delete(j);
                return -1;
            }
            strcat(reply_str, l->val->v.string);
            strcat(reply_str, "\n");
        }
        json_delete(j);
        if (conn) {
            fprintf(conn->cmd_w, "%s\n", reply_str);
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
            fprintf(conn->cmd_w, "%s\n", reply_str);
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
            REVERSE_LIST(jrr->v.array);
            for (struct JsonArray *a = jrr->v.array; a; a = a->next) {
                strcat(rr_str, " ");
                strcat(rr_str, a->val->v.string);
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

        if(conn->mode == TF_JSON){
            fprintf(conn->cmd_w, "%s\n", msg);
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
            fprintf(conn->cmd_w, "%s\n", reply_str);
        }

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
        char *msg = messagequeue_pop(reply_q, -1);
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
    struct addrinfo hints, *res, *rp;
    int status;

    char port_str[15];
    sprintf(port_str, "%u", port);
    bzero(&hints, sizeof(hints));
    hints.ai_family = AF_UNSPEC;     // can be ipv4 or ipv6
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = 0; //TODO AI_NUMERICHOST?
    if ((status = getaddrinfo(address, port_str, &hints, &res)) != 0) {
        log_error("oam_send_reply getaddrinfo for address '%s': %s", address, gai_strerror(status));
        return -1;
    }

    int sock = -1;
    for (rp=res; rp!=NULL; rp=rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock < 0) continue;

        if (sendto(sock, msg, msg_len, 0, rp->ai_addr, rp->ai_addrlen) == 0) {
            log_perror("oam repy sendto");
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

    log_packet("%s %s:%d seq %d lvl %d T - (to %s %d) %s", oam->name, stream, session, seq, level,
               reply_address, port, j_msg);

    oam_send_reply(reply_address, port, j_msg, msg_len);
    free(reply_address);
    free(j_msg);

    json_delete(j);
    return true;
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

    json_object_insert(j, "error", json_string(ping_req->error));

    unsigned msg_len=0;
    char *j_msg = json_serialize(j, &msg_len);

    log_packet("%s %s:%d seq %d lvl %d E - (to %s %d) %s", oam->name, stream, session, seq, level,
               ping_req->return_ip, ping_req->return_port, j_msg);
    oam_send_reply(ping_req->return_ip, ping_req->return_port, j_msg, msg_len);

    free(j_msg);
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
        return false;
    }

    struct oam_request *ping_req = parse_ping_command(cmd->v.string, false, true, NULL);
    ping_req->return_ip = reply_address;
    ping_req->return_port = port;
    if (ping_req->error == NULL) {
        // if (strcmp(oam->stream, ping_req->mep_start->stream_name) != 0) {
            // ping_req->error = strdup("rping target point and ping start point are in different streams");
            // return send_rping_error(oam, p, j, ping_req);
        // }

        struct JsonValue *jstream = json_object_get_string(j, "stream");
        if (jstream == NULL) {
            ping_req->error = strdup("rping request contains no stream name");
            return send_rping_error(oam, p, j, ping_req);
        }
        ping_req->originator_stream = strdup(jstream->v.string);
        ping_req->originator_session_id = session;

        if (!initiate_request(ping_req)) {
            ping_req->error = strdup("ping request could not be sent");
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

    log_packet("%s:%d seq %d lvl %d T (to %s %d) - %s", stream, session, seq, level,
               reply_address, port, j_msg);

    oam_send_reply(reply_address, port, j_msg, msg_len);
    free(reply_address);
    free(j_msg);

    json_delete(j);
    return true;
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

    if (strcmp(jreqt->v.string, "ping") == 0) {
        struct JsonValue *jrr = json_object_get_array(j, "rr");
        if (jrr != NULL) {
            //TODO supply more info: type, level, attached object
            json_array_unshift(jrr, json_string(oam->name));

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
        struct request_msg *msg = messagequeue_pop(request_q, -1);
        if (process_request(msg->oam, msg->pi->packet)) {
            msg->pi->pos += 1;
            pipe_iterator_run(msg->pi);
        } else {
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


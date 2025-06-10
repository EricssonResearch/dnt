// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#define OAM_INTERNAL

#include "oam_session.h"
#include "oam_command.h"
#include "oam_request.h"

#include "log.h"
#include "oam.h"
#include "thread_utils.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <pthread.h>


DEFAULT_LOGGING_MODULE(OAM, INFO);

struct SessionTracker {
    char *conn_name; // NULL if not issued from a command connection
    time_t access_time;
    unsigned interval_ms;
    struct Thread *multireq_thread;
    struct OamRequest *req;
    bool live; //TODO live = req!=NULL
};

struct StreamSessions {
    struct SessionTracker sessions[16];
    unsigned last_session; // last request session on this stream TODO should be local to command thread
};

static struct HashMap *session_ids = NULL; // stream_name -> struct StreamSessions
static pthread_mutex_t session_lock; // should be used to protect the whole session_ids hash


struct CommandConnection *command_connection_for_session(const char *stream_name, unsigned session_id)
{
    struct StreamSessions *ss = get_stream_sessions(stream_name);

    if (ss) {
        struct SessionTracker *sess = &ss->sessions[session_id];
        if (!sess->live) {
            return NULL;
        }

        return find_command_connection(sess->conn_name);
    } else {
        return NULL;
    }
}

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

// @returns true if something was stopped
// @session_lock must be acquired before calling this
static int stop_session_locked(struct SessionTracker *s)
{
    int ret = 0;
    if (s->live) {
        s->multireq_thread = thread_stop(s->multireq_thread);
        free(s->conn_name);
        s->conn_name = NULL;
        s->req = delete_oam_request(s->req);
        s->live = false;
        ret = 1;
    }
    return ret;
}


int alloc_session_id(struct StreamSessions *stream, struct OamRequest *req,
        const char *conn_name, unsigned interval_ms)
{
    pthread_mutex_lock(&session_lock);

    unsigned next_id = (stream->last_session + 1) % 16;
    unsigned id = next_id;
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

    while (stream->sessions[id].live) {
        // "unmask" is fire-and-forget, we can always free its slot
        bool is_unmask = strcmp(request_get_type(stream->sessions[id].req), "unmask") == 0;

        unsigned timeout = MAX(ceil(1.0 + 0.001*stream->sessions[id].interval_ms), 2);
        bool timeout_exceeded = now.tv_sec > stream->sessions[id].access_time + timeout;
        if (timeout_exceeded || is_unmask) {
            //log_info("session %u timeouted", id);
            stop_session_locked(&stream->sessions[id]);
            break;
        }
        id = (id + 1) % 16;
        if (id == next_id) break;
    }

    // We cannot have more than one mask sessions per-stream (per-pipeline)
    // Also if there is a mask session, we have to terminate that first before unmask
    // This is not a good place for that, a simplified session handling would help
    if (!strcmp(request_get_type(req), "unmask")) {
        for (int i=0; i<16; ++i) {
            struct SessionTracker *session = &stream->sessions[i];
            if (!session->live) continue;
            if (!strcmp(request_get_type(session->req), "mask")) {
                stop_session_locked(session);
                break;
            }
        }
    }

    if (stream->sessions[id].live) {
        pthread_mutex_unlock(&session_lock);
        return -1;
    } else {
        stream->last_session = id;
        if (strcmp(request_get_type(req), "unmask") != 0) //FIXME: leaks
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

int stop_session(const char *stream_name, int session)
{
    struct StreamSessions *stream = get_stream_sessions(stream_name);
    if (stream == NULL)
        return -1;
    if (session < 0 || session > 15)
        return 0;
    pthread_mutex_lock(&session_lock);
    if (session==-1)
        session = stream->last_session; //TODO the caller should do this
    int res = stop_session_locked(&stream->sessions[session]);
    pthread_mutex_unlock(&session_lock);
    return res;
}

int stop_all_sessions_of_connection(struct CommandConnection *conn)
{
    FILE *cmd_w = command_connection_get_w(conn);
    int ret = 0;

    pthread_mutex_lock(&session_lock);
    HASHMAP_ITERATE(session_ids, it) {
        const char *stream_name = hash_iterator_key(&it);
        struct StreamSessions *stream = (struct StreamSessions *)hash_iterator_value(&it);

        for (int i=0; i<16; i++) {
            struct SessionTracker *s = &stream->sessions[i];
            if (s->live == false) continue;

            if (command_connection_is_same(conn, s->conn_name)) {
                int res = stop_session_locked(s);
                if (cmd_w) fprintf(cmd_w, "Stopping stream:session %s:%d - %s\n", stream_name, i,
                        res ? "stopped" : "not running");
                ret += res;
            }
        }
    }
    pthread_mutex_unlock(&session_lock);
    return ret;
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
            struct OamRequest *req = stream->sessions[i].req;
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
    pthread_mutex_lock(&session_lock);
    int ret = hashmap_foreach_sorted(session_ids, list_all_sessions_cb, cmd_w);
    pthread_mutex_unlock(&session_lock);
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

void init_session_module(void)
{
    pthread_mutex_init(&session_lock, NULL);
    session_ids = new_hashmap(11, NULL, NULL);
}

void finish_session_module(void)
{
    delete_hashmap(session_ids);
}

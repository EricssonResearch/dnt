// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#define OAM_INTERNAL

#include "oam_session.h"

#include "log.h"
#include "oam.h"
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
    struct OamRequest *req;
    unsigned long long sent;
    unsigned long long recv;
};

struct StreamSessions {
    struct SessionTracker sessions[16];
    unsigned last_session; // last request session on this stream
};

static struct HashMap *session_ids = NULL; // stream_name -> struct StreamSessions
static pthread_mutex_t session_lock; // should be used to protect the whole session_ids hash


struct CommandConnection *command_connection_for_session(const char *stream_name, unsigned session_id)
{
    struct StreamSessions *ss = get_stream_sessions(stream_name);

    if (ss) {
        struct SessionTracker *sess = &ss->sessions[session_id];
        if (!sess->req) {
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
    if (s->req) { //TODO do we need this?
        s->req = delete_oam_request(s->req);
        free(s->conn_name);
        s->conn_name = NULL;
        return 1;
    }
    return 0;
}

static bool session_timeouted(const struct SessionTracker *session, struct timespec now)
{
    unsigned timeout = MAX(ceil(1.0 + 0.001*session->interval_ms), 2);
    return now.tv_sec > session->access_time + timeout;
}


int alloc_session_id(struct OamRequest *req,
        const char *conn_name, unsigned interval_ms)
{
    struct StreamSessions *stream = get_stream_sessions(request_get_stream_name(req));

    pthread_mutex_lock(&session_lock);
    unsigned next_id = (stream->last_session + 1) % 16;
    unsigned id = next_id;
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

    while (stream->sessions[id].req) {
        if (session_timeouted(&stream->sessions[id], now)) {
            //log_info("session %u timeouted", id);
            stop_session_locked(&stream->sessions[id]);
            break;
        }
        id = (id + 1) % 16;
        if (id == next_id) break;
    }

    if (stream->sessions[id].req) {
        pthread_mutex_unlock(&session_lock);
        return -1;
    } else {
        stream->last_session = id;
        stream->sessions[id].access_time = now.tv_sec + 1;
        stream->sessions[id].req = req;
        stream->sessions[id].interval_ms = interval_ms;
        stream->sessions[id].conn_name = conn_name ? strdup(conn_name) : NULL;
        pthread_mutex_unlock(&session_lock);
        return id;
    }
}

int stream_live_session_count(const struct StreamSessions *stream)
{
    int live_session_count = 0;
    struct timespec now;

    pthread_mutex_lock(&session_lock);
    clock_gettime(CLOCK_REALTIME, &now);
    for (int i=0; i<16; i++)
        if (stream->sessions[i].req && !session_timeouted(&stream->sessions[i], now))
            live_session_count++;
    pthread_mutex_unlock(&session_lock);

    return live_session_count;
}

int stop_session(const char *stream_name, unsigned session)
{
    struct StreamSessions *stream = get_stream_sessions(stream_name);
    if (stream == NULL)
        return -1;
    if (session > 15)
        return 0;
    pthread_mutex_lock(&session_lock);
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
            if (s->req == NULL) continue;

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

int list_sessions_of_stream(struct StreamSessions *stream, const char *name, FILE *cmd_w)
{
    bool has_sessions = false;
    for (int i=0; i<16; i++) {
        if (stream->sessions[i].req) {
            has_sessions = true;
        }
    }
    if (!has_sessions) return 0;

    fprintf(cmd_w, "Stream %s sessions:\n", name);
    for (int i=0; i<16; i++) {
        struct OamRequest *req = stream->sessions[i].req;
        if (req) {
            fprintf(cmd_w,"    %d %s %s -> %s level %d connection %s sent %llu recv %llu\n",
                    i, request_get_type(req), request_get_start_name(req),
                    request_get_stop_name(req), request_get_level(req),
                    stream->sessions[i].conn_name ? stream->sessions[i].conn_name : "<background>",
                    stream->sessions[i].sent, stream->sessions[i].recv);
        }
    }
    return 1;
}

static int list_all_sessions_cb(const char *key, void *value, void *userdata)
{
    struct StreamSessions *stream = (struct StreamSessions *)value;
    FILE *cmd_w = (FILE *)userdata;
    list_sessions_of_stream(stream, key, cmd_w);
    return 1;
}

int list_sessions_of_all_streams(FILE *cmd_w)
{
    pthread_mutex_lock(&session_lock);
    int ret = hashmap_foreach_sorted(session_ids, list_all_sessions_cb, cmd_w);
    pthread_mutex_unlock(&session_lock);
    return ret;
}

void session_recv(const char *stream_name, unsigned session)
{
    struct StreamSessions *stream = get_stream_sessions(stream_name);
    if (stream == NULL)
        return;

    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    stream->sessions[session].access_time = now.tv_sec + 1;
    stream->sessions[session].recv += 1;
}

void session_sent(struct StreamSessions *stream, unsigned session)
{
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    stream->sessions[session].access_time = now.tv_sec + 1;
    stream->sessions[session].sent += 1;
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

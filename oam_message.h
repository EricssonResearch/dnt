// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_OAM_MESSAGE_H
#define R2_OAM_MESSAGE_H

#ifndef OAM_INTERNAL
#error "this header is internal to the OAM module"
#endif

#include <stdio.h>


struct CommandConnection;

struct OamRequest;
struct StreamSessions;
struct Thread;

struct StreamSessions *get_stream_sessions(const char *stream_name);

int alloc_session_id(struct StreamSessions *stream, struct OamRequest *req,
        const char *conn_name, unsigned interval_ms);

int stream_live_session_count(const struct StreamSessions *stream);

void stop_session(const char *stream_name, int session, struct CommandConnection *conn);

void stop_all_sessions_of_connection(struct CommandConnection *conn);

int list_sessions_of_stream(struct StreamSessions *stream, FILE *cmd_w);

int list_sessions_of_all_streams(FILE *cmd_w);

void session_set_thread(struct StreamSessions *stream, int session, struct Thread *th);

struct Thread *session_get_thread(struct StreamSessions *stream, int session);

void session_touch(struct StreamSessions *stream, int session);

void init_msg_module(bool have_reply_iface);

void finish_msg_module(void);

#endif // R2_OAM_MESSAGE_H

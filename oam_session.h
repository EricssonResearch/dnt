// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_OAM_SESSION_H
#define R2_OAM_SESSION_H

#ifndef OAM_INTERNAL
#error "this header is internal to the OAM module"
#endif

#include "oam_command.h"
#include "oam_request.h"
#include "thread_utils.h"

#include <stdio.h>

// keeps the OAM sessions going on a stream
struct StreamSessions;

// adds a reference to the connection, it must be released with release_command_connection()
struct CommandConnection *command_connection_for_session(const char *stream_name, unsigned session_id);

struct StreamSessions *get_stream_sessions(const char *stream_name);

// allocate a new session id for @req that will be sent in @stream
// @conn_name is the name of the command connection of @req
// TODO request_get_conn_name
// @interval_ms is the send interval for periodic requests (used for calculating timeout)
int alloc_session_id(struct StreamSessions *stream, struct OamRequest *req,
        const char *conn_name, unsigned interval_ms);

int stream_live_session_count(const struct StreamSessions *stream);

// @returns the number of sessions stopped, or -1 on no such stream
int stop_session(const char *stream_name, int session);

// @returns the number of sessions stopped
int stop_all_sessions_of_connection(struct CommandConnection *conn);

int list_sessions_of_stream(struct StreamSessions *stream, const char *name, FILE *cmd_w);

int list_sessions_of_all_streams(FILE *cmd_w);

// update the last used time of the session to now
// sessions that haven't been touched in a while are assumed to be timed out
void session_touch(struct StreamSessions *stream, int session);

void init_session_module(void);

void finish_session_module(void);

#endif // R2_OAM_SESSION_H

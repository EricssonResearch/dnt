// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_OAM_MESSAGE_H
#define R2_OAM_MESSAGE_H

#ifndef OAM_INTERNAL
#error "this header is internal to the OAM module"
#endif

#include <stdio.h>

//TODO make the internals private? the parse_*_command() functions would suffer
//  TODO oam_request.ch
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

//struct StreamSessions;

struct command_connection;

struct StreamSessions *get_stream_sessions(const char *stream_name);

bool known_stream(const char *stream_name);

int stream_live_session_count(const struct StreamSessions *stream);

void stop_session(const char *stream_name, int session, struct command_connection *conn);

void stop_all_sessions_of_connection(struct command_connection *conn);

int list_sessions_of_stream(struct StreamSessions *stream, FILE *cmd_w);

int list_sessions_of_all_streams(FILE *cmd_w);


struct oam_request *new_oam_request(const char *type, struct command_connection *conn);

struct oam_request *delete_oam_request(struct oam_request *req);

// returns true on success
bool initiate_request(struct oam_request *req);

void init_msg_module(bool have_command_iface, bool have_reply_iface);

void finish_msg_module(void);

#endif // R2_OAM_MESSAGE_H

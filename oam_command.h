// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_OAM_COMMAND_H
#define R2_OAM_COMMAND_H

#ifndef OAM_INTERNAL
#error "this header is internal to the OAM module"
#endif

#include <stdio.h>

enum TerminalFormat {
    TF_DUMP, //TODO why is this called dump mode?
    TF_JSON,
};

//TODO make the internals private
struct command_connection {
    char *name;
    int socket_fd; // RW
    FILE *cmd_w; // WRONLY
    int w_users;
    enum TerminalFormat mode;
    struct Thread *thread;
};

struct oam_request;

const char *terminal_format_name(enum TerminalFormat f);

struct command_connection *find_command_connection(const char *name);

bool command_connection_is_same(const struct command_connection *conn, const char *name);

FILE *command_connection_get_w(struct command_connection *conn);

void command_connection_release_w(struct command_connection *conn);

// always returns a request, sets ret->error to an error message
struct oam_request *parse_ping_command(const char *oam_command, bool allow_returniface, bool allow_num,
        struct command_connection *conn);

// always returns a request, sets ret->error to an error message
struct oam_request *parse_rping_command(const char *oam_command,
        struct command_connection *conn);

// always returns a request, sets ret->error to an error message
struct oam_request *parse_rlist_command(const char *oam_command,
        struct command_connection *conn);

void init_cmd_module(void);

void finish_cmd_module(void);

#endif // R2_OAM_COMMAND_H

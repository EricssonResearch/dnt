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

struct command_connection;

const char *terminal_format_name(enum TerminalFormat f);

struct command_connection *find_command_connection(const char *name);

bool command_connection_is_same(const struct command_connection *conn, const char *name);

FILE *command_connection_get_w(struct command_connection *conn);

void command_connection_release_w(struct command_connection *conn);

enum TerminalFormat command_connection_get_format(const struct command_connection *conn);

void init_cmd_module(void);

void finish_cmd_module(void);

#endif // R2_OAM_COMMAND_H

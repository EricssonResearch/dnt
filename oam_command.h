// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef DNT_OAM_COMMAND_H
#define DNT_OAM_COMMAND_H

#ifndef OAM_INTERNAL
#error "this header is internal to the OAM module"
#endif

#include "hashmap.h"

#include <stdio.h>
#include <stdbool.h>

enum TerminalFormat {
    TF_NORMAL,
    TF_JSON,
};

struct CommandConnection;

// acquire a reference to command connection named @name
// @returns NULL if there is no such connection
// the returned reference MUST be released with @release_command_connection
struct CommandConnection *find_command_connection(const char *name);

// release the reference to @conn
void release_command_connection(struct CommandConnection *conn);

// they are the same if @conn has the same name as @name
bool command_connection_is_same(const struct CommandConnection *conn, const char *name);

// @returns the write filehandle of the command interface
FILE *command_connection_get_w(struct CommandConnection *conn);

enum TerminalFormat command_connection_get_format(const struct CommandConnection *conn);

void init_cmd_module(void);

void finish_cmd_module(void);

#endif // DNT_OAM_COMMAND_H

// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_CONF_ACTIONS_H
#define R2_CONF_ACTIONS_H

#include "inifile.h"
#include "parsetree.h"
#include "pipeline.h"

struct ConfAction;

// parse the "*:actions" line for a stream
// @ifaces and @objects are the new ones added in the transaction
// @returns a linked list of (opaque) action descriptors
struct ConfAction *parse_actions_line(const char *stream, const char *line,
        const struct HeaderDescriptor *headers,
        const struct HashMap *ifaces,
        const struct HashMap *objects,
        const struct IniSection *streams_sec);

// always returns NULL
struct ConfAction *delete_confaction_list(struct ConfAction *ca_list);

// creates an action pipeline from the action descriptor list
// @returns NULL if @ca_list is empty
struct Pipeline *assemble_actions(const char *stream_name, const struct ConfAction *ca_list);

void confactions_print(const struct ConfAction *ca_list, unsigned indent);

#endif // R2_CONF_ACTIONS_H

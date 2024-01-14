// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_CONF_ACTIONS_H
#define R2_CONF_ACTIONS_H

struct ConfAction;
struct Action;
struct HeaderDescriptor;
struct HashMap;
struct IniSection;
struct Interface;

// parse the "*:actions" line for a stream
// modifies the given @line
// returns a linked list of (opaque) action descriptors
struct ConfAction *parse_actions_line(const char *stream, char *line,
        const struct HeaderDescriptor *headers,
        struct HashMap *ifaces,
        struct HashMap *objects, struct IniSection *streams_sec);

// always returns NULL
struct ConfAction *delete_confaction_list(struct ConfAction *ca_list);

// creates an Action array from the action descriptor list
struct Action *assemble_actions(const char *stream_name, const struct ConfAction *ca_list, unsigned *action_count);

void confactions_print(const struct ConfAction *ca_list);

#endif // R2_CONF_ACTIONS_H

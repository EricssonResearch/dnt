
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
struct ConfAction *process_actions_line(const char *stream, char *line,
        const struct HeaderDescriptor *headers,
        struct Interface *ifaces, unsigned ifcount, //TODO all pointers const
        struct HashMap *objects, struct IniSection *streams_sec);

// creates an Action array from the action descriptor list
struct Action *assemble_actions(const struct ConfAction *ca_list, unsigned *action_count);

void print_actions(const struct ConfAction *ca_list);

#endif // R2_CONF_ACTIONS_H

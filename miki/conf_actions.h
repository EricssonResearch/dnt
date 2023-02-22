
#ifndef R2_CONF_ACTIONS_H
#define R2_CONF_ACTIONS_H

struct ConfAction;
struct Action;
struct ConfHeader;
struct Interface;

// parse the "*:actions" line for a stream
// modifies the given string
// returns a linked list of (opaque) action descriptors
// TODO this will need some more parameters: object list, handle for the ini section
struct ConfAction *process_actions(const char *stream, char *line, struct ConfHeader *headers,
        struct Interface *ifaces, unsigned ifcount);

struct Action *assemble_actions(struct ConfAction *ca_list, unsigned *action_count);

void print_actions(struct ConfAction *ca_list);

#endif // R2_CONF_ACTIONS_H

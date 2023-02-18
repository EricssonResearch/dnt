
#ifndef R2_CONF_ACTIONS_H
#define R2_CONF_ACTIONS_H

struct Action;
struct ConfHeader;
struct Interface;

// parse the "*:actions" line for a stream
// modifies the given string
// returns an array of actions
// TODO this will need some more parameters: object list, handle for the ini section
struct Action *process_actions(const char *stream, char *line, struct ConfHeader *headers,
        struct Interface *ifaces, unsigned ifcount,
        unsigned *action_count);

#endif // R2_CONF_ACTIONS_H

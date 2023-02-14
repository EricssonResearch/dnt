
#ifndef R2_CONF_ACTIONS_H
#define R2_CONF_ACTIONS_H

struct Action;
struct ConfHeader;

// parse the "*:actions" line for a stream
// modifies the given string
// TODO this will need some more parameters: interface list, object list, handle for the ini section
struct Action *process_actions(const char *stream, char *line, struct ConfHeader *headers, unsigned *action_count);

#endif // R2_CONF_ACTIONS_H

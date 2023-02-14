
#ifndef R2_CONF_PACKET_H
#define R2_CONF_PACKET_H

struct ConfHeaderMatch {
    char *fieldname;
    char *fieldvalue; //TODO when do we parse this?
    struct ConfHeaderMatch *next;
};

// this is public because conf_actions needs the list
struct ConfHeader {
    char *type;
    char *name;
    int id;
    struct ConfHeaderMatch *matches; //TODO hash table instead of linked list?

    struct ConfHeader *next;
};

// parse the "*:packet" line for a stream
// @stream name is used in error messages
// modifies the given @line
struct ConfHeader *process_packet(const char *stream, char *line);

struct ConfHeader *delete_header_list(struct ConfHeader *headers);

#endif // R2_CONF_PACKET_H


#ifndef R2_CONF_PACKET_H
#define R2_CONF_PACKET_H

struct ConfHeaderMatch {
    char *fieldname;
    char *fieldvalue; //TODO when do we parse this?
    struct ConfHeaderMatch *next;
};

enum ConfHeaderFlag {
    CH_PACKET, // header comes from the :packet line
    CH_NEW, // header was added in an action
    CH_DEL, // header was removed by an action
};

// this is public because conf_actions needs the list
struct ConfHeader {
    char *type;
    char *name;
    int id; // protocol type id
    enum ConfHeaderFlag state;
    struct ConfHeaderMatch *matches; //TODO hash table instead of linked list?
    //TODO indicate somehow if this is payload? name=NULL?
    struct ConfHeader *next;
};

// parse the "*:packet" line for a stream
// @stream name is used in error messages
// modifies the given @line
struct ConfHeader *process_packet(const char *stream, char *line);

struct ConfHeader *delete_header_list(struct ConfHeader *headers);

struct ConfHeader *header_list_find_name(struct ConfHeader *headers, const char *name);

struct ConfHeader *header_list_find_typeid(struct ConfHeader *headers, int id);

// header name is in the form of "type_identifier", this extracts the type
// @returns a new string
char *header_type_from_name(const char *name);

#endif // R2_CONF_PACKET_H

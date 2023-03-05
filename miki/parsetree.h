
#ifndef R2_PARSETREE_H
#define R2_PARSETREE_H

#include "transfer.h"

#include <stdbool.h>

struct ConfStream;
struct Interface;
struct Packet;
struct Pipeline;

struct HeaderMatch {
    struct ProtocolField *field;
    struct Value value;
    struct HeaderMatch *next;
};

struct HeaderDescriptor {
    char *type;
    char *name; // type[_identifier]
    int id; // protocol type id
    struct HeaderMatch *matches; //TODO hash table instead of linked list?

    struct HeaderDescriptor *next;
};

struct ParseTree;

struct ParseTree *new_parsetree(struct Interface *iface);

void parsetree_ref(struct ParseTree *pt);

void parsetree_unref(struct ParseTree *pt);

// adds a new stream to the decision tree
// the @state of each header must be CH_PACKET
// this will add a reference to @pipe
// this should not alter @headers, dynconf will need it later
bool parsetree_add_stream(struct ParseTree *pt, struct HeaderDescriptor *headers, struct Pipeline *pipe);

//TODO parsetree_del_stream()

// parses the packet:
//      - identify headers, fill p->headers
//      - match header field values against known streams
// @returns an action pipeline to process the packet or NULL if unknown stream
struct Pipeline *parsetree_process(struct ParseTree *pt, struct Packet *p);

// always returns NULL
struct HeaderDescriptor *delete_header_list(struct HeaderDescriptor *headers);

struct HeaderDescriptor *header_list_find_by_name(struct HeaderDescriptor *headers, const char *name);

struct HeaderDescriptor *header_list_find_by_typeid(struct HeaderDescriptor *headers, int id);


#endif // R2_PARSETREE_H

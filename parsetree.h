// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_PARSETREE_H
#define R2_PARSETREE_H

#include "header.h"
#include "protocol.h"
#include "value.h"

#include <stdbool.h>

struct Interface;
struct Packet;
struct Pipeline;

struct HeaderMatch {
    struct HeaderField field;
    struct Value value;
    value_comparator *comparator;

    struct HeaderMatch *next;
};

struct HeaderDescriptor {
    char *name; // format: type[_identifier]
    enum ProtocolID id;
    struct HeaderMatch *matches;

    struct HeaderDescriptor *next;
};

struct ParseTree;

struct ParseTree *new_parsetree(struct Interface *iface);

void parsetree_ref(struct ParseTree *pt);

void parsetree_unref(struct ParseTree *pt);

// adds a new stream to the decision tree
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
struct HeaderMatch *delete_match_list(struct HeaderMatch *matches);

// always returns NULL
struct HeaderDescriptor *delete_header_list(struct HeaderDescriptor *headers);

struct HeaderDescriptor *header_list_find_by_name(struct HeaderDescriptor *headers, const char *name);

struct HeaderDescriptor *header_list_find_by_typeid(struct HeaderDescriptor *headers, enum ProtocolID id);

#endif // R2_PARSETREE_H

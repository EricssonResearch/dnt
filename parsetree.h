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
    struct Value value; // currently always a constant
    value_comparator *comparator;

    struct HeaderMatch *next;
};

struct HeaderDescriptor {
    char *name; // format: type[_identifier]
    enum ProtocolID id;
    struct HeaderMatch *matches;

    struct HeaderDescriptor *next;
};

// always returns NULL
struct HeaderDescriptor *delete_header_list(struct HeaderDescriptor *headers);

struct HeaderDescriptor *copy_header_list(const struct HeaderDescriptor *headers, bool copy_matchlist);

struct HeaderDescriptor *header_list_find_by_name(struct HeaderDescriptor *headers, const char *name);

struct HeaderDescriptor *header_list_find_by_typeid(struct HeaderDescriptor *headers, enum ProtocolID id);


struct ParseTree;

struct ParseTree *new_parsetree(const struct Interface *iface);

// always returns NULL
struct ParseTree *delete_parsetree(struct ParseTree *pt);

// adds a new stream to the decision tree
// the name of the stream is @pipe->name
// @returns false if a stream with this name is already in the decision tree
// the add order matters: if a packet matches multiple streams, the first one added will match
//      TODO we could introduce a priority for reordering
// this will make a deep copy of @headers (easy to copy, difficult to refcount)
// this will add a reference to @pipe (difficult to copy)
bool parsetree_add_stream(struct ParseTree *pt, struct HeaderDescriptor *headers, struct Pipeline *pipe);

// removes a stream from the decision tree
// @returns true on success, false if the stream is unknown
bool parsetree_del_stream(struct ParseTree *pt, const char *stream_name);

// Checks if streams are empty.
// @returns true on empty, false if there are streams
bool parsetree_streams_empty(struct ParseTree *pt);

// replaces an existing stream in the decision tree
// the name of the stream is @pipe->name
// @returns false if a stream with this name is not found in the decision tree
// this will make a deep copy of @headers (easy to copy, difficult to refcount)
// this will add a reference to @pipe (difficult to copy)
bool parsetree_replace_stream(struct ParseTree *pt, struct HeaderDescriptor *headers, struct Pipeline *pipe);

// parses the packet:
//      - identify headers, fill p->headers
//      - match header field values against known streams
// @returns an action pipeline iterator to process the packet or NULL if unknown stream
struct PipelineIterator *parsetree_identify(struct ParseTree *pt, struct Packet *p);

#endif // R2_PARSETREE_H

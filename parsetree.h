// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef DNT_PARSETREE_H
#define DNT_PARSETREE_H

#include "headerdesc.h"
#include "interface.h"
#include "packet.h"
#include "pipeline.h"

#include <stdbool.h>

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

// @returns true if @pt has no streams
bool parsetree_empty(const struct ParseTree *pt);

// replaces an existing stream in the decision tree
// the name of the stream is @pipe->name
// @returns false if a stream with this name is not found in the decision tree
// this will make a deep copy of @headers (easy to copy, difficult to refcount)
// this will add a reference to @pipe (difficult to copy)
bool parsetree_replace_stream(struct ParseTree *pt, struct HeaderDescriptor *headers, struct Pipeline *pipe);

// parses the packet:
//      - identify headers, fill p->headers
//      - match header field values against known streams
// assumes p->len bytes of continuous packet data at p->start
// @returns an action pipeline iterator to process the packet or NULL if unknown stream
struct PipelineIterator *parsetree_identify(struct ParseTree *pt, struct Packet *p);

// prints information about @pt to @cmd_w
// intended for @iface_print_info of the 'iface' telnet command
void parsetree_print_info(const struct ParseTree *pt, FILE *cmd_w);

#endif // DNT_PARSETREE_H

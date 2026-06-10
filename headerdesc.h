// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef DNT_HEADERDESC_H
#define DNT_HEADERDESC_H

#include "header.h"

struct HeaderMatch {
    struct HeaderField field;
    struct Value value; // currently always a constant
    value_comparator *comparator;
    bool neg;

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


#endif // DNT_HEADERDESC_H

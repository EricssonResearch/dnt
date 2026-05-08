// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.

#include "headerdesc.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>


static struct HeaderMatch *delete_match_list(struct HeaderMatch *matches)
{
    struct HeaderMatch *m = matches;
    while (m) {
        struct HeaderMatch *d = m;
        m = m->next;
        free(d->value.value);
        free(d);
    }
    return NULL;
}

static struct HeaderMatch *copy_match_list(const struct HeaderMatch *matches)
{
    struct HeaderMatch *ret = NULL;
    struct HeaderMatch *r = NULL;

    while (matches) {
        struct HeaderMatch *w = calloc_struct(HeaderMatch);
        *w = *matches;
        // we must manually copy the constant value
        unsigned bits_total = w->value.bitoffset + w->value.bitcount;
        unsigned bytes_total = DIVCEIL(bits_total, 8);
        w->value.value = memdup(matches->value.value, bytes_total);

        if (r) {
            r->next = w;
            r = w;
        } else {
            ret = r = w;
        }

        matches = matches->next;
    }

    return ret;
}

struct HeaderDescriptor *delete_header_list(struct HeaderDescriptor *headers)
{
    struct HeaderDescriptor *h = headers;
    while (h) {
        struct HeaderDescriptor *d = h;
        h = h->next;
        free(d->name);
        delete_match_list(d->matches);
        free(d);
    }
    return NULL;
}

// deep copy, including the match list
struct HeaderDescriptor *copy_header_list(const struct HeaderDescriptor *headers, bool copy_matchlist)
{
    struct HeaderDescriptor *ret = NULL;
    struct HeaderDescriptor *r = NULL;

    while (headers) {
        struct HeaderDescriptor *w = calloc_struct(HeaderDescriptor);
        w->name = strdup(headers->name);
        w->id = headers->id;
        if (copy_matchlist) w->matches = copy_match_list(headers->matches);

        if (r) {
            r->next = w;
            r = w;
        } else {
            ret = r = w;
        }

        headers = headers->next;
    }

    return ret;
}

struct HeaderDescriptor *header_list_find_by_name(struct HeaderDescriptor *headers, const char *name)
{
    struct HeaderDescriptor *h = headers;
    while (h) {
        if (strcmp(h->name, name) == 0)
            return h;
        h = h->next;
    }
    return NULL;
}

struct HeaderDescriptor *header_list_find_by_typeid(struct HeaderDescriptor *headers, enum ProtocolID id)
{
    struct HeaderDescriptor *h = headers;
    while (h) {
        if (h->id == id)
            return h;
        h = h->next;
    }
    return NULL;
}


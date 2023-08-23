// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "parsetree.h"
#include "interface.h"
#include "packet.h"
#include "pipeline.h"
#include "protocol.h"
#include "utils.h"

#include "conf_streams.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct ParseTree {
    struct Interface *iface;
    struct HeaderDescriptor *headers;
    struct Pipeline *pipe;
    unsigned reference_count;
    struct ParseTree *next; // its a list for now
};


struct ParseTree *new_parsetree(struct Interface *iface)
{
    struct ParseTree *ret = calloc_struct(ParseTree);
    ret->iface = iface;
    return ret;
}

void parsetree_ref(struct ParseTree *pt)
{
    __atomic_fetch_add(&pt->reference_count, 1, __ATOMIC_RELAXED);
}

void parsetree_unref(struct ParseTree *pt)
{
    if (pt->reference_count > 0)
        __atomic_fetch_sub(&pt->reference_count, 1, __ATOMIC_RELAXED);

    if (pt->reference_count == 0) {
        if (pt->pipe) {
            pipeline_unref(pt->pipe);
        }
        free(pt);
    }
}

bool parsetree_add_stream(struct ParseTree *pt_head, struct HeaderDescriptor *headers, struct Pipeline *pipe)
{
    struct ParseTree *pt_last = pt_head;
    while (pt_last->next) {
        pt_last = pt_last->next;
    }
    // TODO: handle refereneces
    pt_last->next = new_parsetree(pt_head->iface);
    pt_last->headers = headers;
    pt_last->pipe = pipe;
    pipeline_ref(pipe);
    return true;
}

static bool parsetree_match_header(const struct HeaderMatch *fields, const struct Packet *p)
{
    const struct HeaderMatch *f = fields;
    bool matched = true;
    while (f) {
        matched &= f->comparator(f->field, &f->value, p);
        f = f->next;
    }
    return matched;
}

struct Pipeline *parsetree_process(struct ParseTree *pt_head, struct Packet *p)
{
    for (struct ParseTree *pt = pt_head; pt != NULL; pt = pt->next) {
        if (pt->headers == NULL) {
            fprintf(stderr, "parsetree %s has no streams\n", pt->iface->name);
            return NULL;
        }

        struct HeaderDescriptor *h = pt->headers;
        unsigned offset = 0;
        bool full_stream_match = true;
        while (h) {
            const struct Protocol *proto = &protocol_list[h->id];
            // if a header fails to match, we dont check the next one
            packet_identify_header(p, h->id, offset, proto->bytelength);
            if(!parsetree_match_header(h->matches, p)) {
                full_stream_match = false;
                packet_clear_headers(p);
                break;
            }
            offset += proto->bytelength;
            h = h->next;
        }
        if (full_stream_match) {
            packet_identify_header(p, PROTO_ID_PAYLOAD, offset, p->len-offset);
            return pt->pipe;
        }
    }
    return NULL;
}

struct HeaderMatch *delete_match_list(struct HeaderMatch *matches)
{
    struct HeaderMatch *m = matches;
    while (m) {
        struct HeaderMatch *d = m;
        m = m->next;
        free(d->value.value);
        free(d->field);
        free(d);
    }
    return NULL;
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



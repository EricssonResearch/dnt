
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
    pt->reference_count++;
}

void parsetree_unref(struct ParseTree *pt)
{
    if (pt->reference_count > 0)
        pt->reference_count--;

    if (pt->reference_count == 0) {
        if (pt->pipe) {
            pipeline_unref(pt->pipe);
        }
        free(pt);
    }
}

bool parsetree_add_stream(struct ParseTree *pt_head, struct HeaderDescriptor *headers, struct Pipeline *pipe)
{
    struct ParseTree *pt_new = pt_head;
    while (pt_new->next) {
        pt_new = pt_new->next;
    }
    // TODO: handle refereneces
    pt_new->next = new_parsetree(pt_head->iface);
    pt_new->headers = headers;
    pt_new->pipe = pipe;
    pipeline_ref(pipe);
    return true;
}

//static bool compare_bytes(const void *state, const struct Value *value, const struct Packet *p)
//state ---> HeaderField
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
        if (p->from != pt_head->iface) {
            fprintf(stderr, "wrong parsetree %s %s\n", pt_head->iface->name, p->from->name);
            return NULL;
        }
        if (pt_head->headers == NULL) {
            fprintf(stderr, "parsetree %s has no streams\n", pt_head->iface->name);
            return NULL;
        }

        // TODO: check that these headers really exist in the packet :)
        struct HeaderDescriptor *h = pt_head->headers;
        unsigned offset = 0;
        bool full_stream_match = true;
        while (h) {
            struct Protocol *proto = &protocol_list[h->id];
            // if a header fails to match, we dont check the next one
            if(!parsetree_match_header(h->matches, p)) {
                full_stream_match = false;
                break;
            }
            packet_identify_header(p, h->id, offset, proto->bytelength);
            offset += proto->bytelength;
            h = h->next;
        }
        if (full_stream_match) {
            packet_identify_header(p, PROTO_ID_PAYLOAD, offset, p->len-offset);
            return pt_head->pipe;
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
        free(d->type);
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

struct HeaderDescriptor *header_list_find_by_typeid(struct HeaderDescriptor *headers, int id)
{
    struct HeaderDescriptor *h = headers;
    while (h) {
        if (h->id == id)
            return h;
        h = h->next;
    }
    return NULL;
}



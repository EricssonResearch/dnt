
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

bool parsetree_add_stream(struct ParseTree *pt, struct HeaderDescriptor *headers, struct Pipeline *pipe)
{
    //TODO support more than one stream per interface :)
    pt->headers = headers;
    pt->pipe = pipe;
    pipeline_ref(pipe);
    return true;
}

struct Pipeline *parsetree_process(struct ParseTree *pt, struct Packet *p)
{
    if (p->from != pt->iface) {
        fprintf(stderr, "wrong parsetree %s %s\n", pt->iface->name, p->from->name);
        return NULL;
    }
    if (pt->headers == NULL) {
        fprintf(stderr, "parsetree %s has no streams\n", pt->iface->name);
        return NULL;
    }

    //TODO check that these headers really exist in the packet :)
    struct HeaderDescriptor *h = pt->headers;
    unsigned offset = 0;
    while (h) {
        const struct Protocol *proto = &protocol_list[h->id];
        packet_identify_header(p, h->id, offset, proto->bytelength);
        offset += proto->bytelength;
        h = h->next;
    }
    packet_identify_header(p, PROTO_ID_PAYLOAD, offset, p->len-offset);

    return pt->pipe;
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



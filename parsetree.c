// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "parsetree.h"
#include "interface.h"
#include "log.h"
#include "notification.h"
#include "packet.h"
#include "pipeline.h"
#include "protocol.h"
#include "utils.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

DEFAULT_LOGGING_MODULE(PARSER, WARNING);


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


struct Stream {
    struct HeaderDescriptor *headers;
    struct Pipeline *pipe;
    unsigned long long match_count;
    unsigned long long match_bytes;
    struct Stream *next;
};

struct ParseTree {
    const struct Interface *iface;
    struct Stream *streams;
    unsigned long long nomatch_count;
    unsigned long long nomatch_bytes;
};

static struct Stream *new_stream(struct HeaderDescriptor *headers, struct Pipeline *pipe)
{
    struct Stream *news = calloc_struct(Stream);
    news->headers = copy_header_list(headers, true);
    news->pipe = pipe;
    pipeline_ref(pipe);
    return news;
}

static struct Stream *delete_stream(struct Stream *del)
{
    pipeline_unref(del->pipe);
    delete_header_list(del->headers);
    free(del);
    return NULL;
}

static NotificationLevel pt_notification_pull_fn(void *self, struct JsonValue **msg)
{
    struct ParseTree *pt = (struct ParseTree *)self;
    char name[1024];

    struct JsonValue *js = json_object();
    json_object_insert(js, "no match count", json_number(pt->nomatch_count));
    json_object_insert(js, "no match bytes", json_number(pt->nomatch_bytes));
    for (struct Stream *s=pt->streams; s; s=s->next) {
        snprintf(name, sizeof(name), "%s count", s->pipe->name);
        json_object_insert(js, name, json_number(s->match_count));
        snprintf(name, sizeof(name), "%s bytes", s->pipe->name);
        json_object_insert(js, name, json_number(s->match_bytes));
    }
    *msg = js;
    return NOTIF_INFO;
}

struct ParseTree *new_parsetree(const struct Interface *iface)
{
    struct ParseTree *ret = calloc_struct(ParseTree);
    ret->iface = iface;
    char name[1024];
    snprintf(name, sizeof(name), "%s parser", iface->name);
    notification_register_source(name, pt_notification_pull_fn, ret, 2000);
    return ret;
}

struct ParseTree *delete_parsetree(struct ParseTree *pt)
{
    if (pt == NULL) return NULL;

    char name[1024];
    snprintf(name, sizeof(name), "%s parser", pt->iface->name);
    notification_register_source(name, NULL, NULL, 2000);

    struct Stream *s = pt->streams;
    while (s) {
        struct Stream *d = s;
        s = s->next;
        delete_stream(d);
    }
    free(pt);

    return NULL;
}

bool parsetree_streams_empty(struct ParseTree *pt)
{
    return (pt->streams == NULL);
}

bool parsetree_add_stream(struct ParseTree *pt, struct HeaderDescriptor *headers, struct Pipeline *pipe)
{
    for (struct Stream *s=pt->streams; s; s=s->next) {
        if (strcmp(s->pipe->name, pipe->name) == 0) return false;
    }

    struct Stream *news = new_stream(headers, pipe);

    if (pt->streams) {
        struct Stream *s = pt->streams;
        while (s->next) s = s->next;
        s->next = news;
    } else {
        pt->streams = news;
    }

    return true;
}

bool parsetree_del_stream(struct ParseTree *pt, const char *stream_name)
{
    if (pt->streams == NULL) return false;

    if (strcmp(pt->streams->pipe->name, stream_name) == 0) {
        // remove the first item
        struct Stream *d = pt->streams;
        pt->streams = d->next;
        delete_stream(d);
        return true;
    }

    for (struct Stream *s=pt->streams; s->next; s=s->next) {
        if (strcmp(s->next->pipe->name, stream_name) == 0) {
            // remove s->next
            struct Stream *d = s->next;
            s->next = d->next;
            delete_stream(d);
            return true;
        }
    }
    return false;
}

bool parsetree_replace_stream(struct ParseTree *pt, struct HeaderDescriptor *headers, struct Pipeline *pipe)
{
    for (struct Stream *s=pt->streams; s; s=s->next) {
        if (strcmp(s->pipe->name, pipe->name) == 0) {
            pipeline_unref(s->pipe);
            delete_header_list(s->headers);
            s->headers = copy_header_list(headers, true);
            s->pipe = pipe;
            pipeline_ref(pipe);
            return true;
        }
    }
    return false;
}

static bool parsetree_match_header(const struct HeaderMatch *fields, const struct Packet *p)
{
    const struct HeaderMatch *f = fields;
    while (f) {
        if (!f->comparator(&f->field, &f->value, p)) return false;
        f = f->next;
    }
    return true;
}

struct PipelineIterator *parsetree_identify(struct ParseTree *pt, struct Packet *p)
{
    for (struct Stream *s=pt->streams; s; s=s->next) {
        struct HeaderDescriptor *h = s->headers;
        unsigned offset = 0;
        bool full_stream_match = true;

        log_debug("trying %s", s->pipe->name);

        while (h) {
            const struct Protocol *proto = protocol_from_id(h->id);
            // if a header fails to match, we dont check the next one
            if (!packet_identify_header(p, h->id, offset, proto->bytelength)) {
                full_stream_match = false;
                packet_clear_headers(p);
                break;
            }
            if (!parsetree_match_header(h->matches, p)) {
                full_stream_match = false;
                packet_clear_headers(p);
                break;
            }
            offset += proto->bytelength;
            h = h->next;
        }

        if (full_stream_match) {
            packet_identify_header(p, PROTO_ID_PAYLOAD, offset, p->len-offset);

            log_packet("identified %u headers, pipe %s", p->header_count, s->pipe->name);
            packet_logcat(p, "%s ", s->pipe->name);
            for (unsigned i=0; i<p->header_count; i++) {
                log_packet("  header %u is %s at %u len %u", i,
                        protocol_from_id(p->headers[i].type)->name, p->headers[i].start, p->headers[i].len);
                packet_logcat(p, "|%s", protocol_from_id(p->headers[i].type)->name);
            }
            packet_logcat(p, "| ");
            s->match_count++;
            s->match_bytes += packet_length(p);

            return new_pipe_iterator(s->pipe, p);
        }
    }

    log_packet("no pipeline found, unknown stream");
    packet_logcat(p, "unknown stream");
    pt->nomatch_count++;
    pt->nomatch_bytes += packet_length(p);
    return NULL;
}



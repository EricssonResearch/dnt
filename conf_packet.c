// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "conf_packet.h"
#include "conf_utils.h"
#include "header.h"
#include "log.h"
#include "parsetree.h"
#include "protocol.h"
#include "utils.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

DEFAULT_LOGGING_MODULE(CONFIG, WARNING);

struct StageState {
    const char *stream;
    struct HeaderDescriptor *headers;
    struct HeaderDescriptor *current_header;
    unsigned current_idx;
};

static bool process_packet_token(char *token, void *userdata)
{
#define THROW(msg, ...)                         \
    do {                                        \
        log_error("stream %s: " msg,            \
                stst->stream, ##__VA_ARGS__);   \
        return false;                           \
    } while (0)

    struct StageState *stst = (struct StageState *)userdata;

    if (stst->headers->name) {
        // here we don't want parameters for the headers
        THROW("header %s invalid extra parameter '%s'", stst->headers->name, token);
    } else {
        char *type = header_type_from_name(token);
        stst->headers->id = protocol_id_from_type(type);
        free(type);
        if (stst->headers->id < 0) {
            THROW("unknown protocol '%s'", token);
        }
        stst->headers->name = strdup(token);
    }

    return true;
#undef THROW
}

static bool process_packet_stage(char *stage, void *userdata)
{
#define THROW(msg, ...)                                             \
    do {                                                            \
        log_error("stream %s: " msg,                                \
                stst->stream, ##__VA_ARGS__);                       \
        return false;                                               \
    } while (0)

    struct StageState *stst = (struct StageState *)userdata;

    struct HeaderDescriptor *newheader = calloc_struct(HeaderDescriptor);
    newheader->next = stst->headers;
    stst->headers = newheader;

    if (!foreach_tokens(stage, process_packet_token, stst)) {
        THROW("invalid header in packet line");
    }

    if (stst->headers->name == NULL) {
        THROW("missing header name in packet line");
    }

    return true;
#undef THROW
}


static bool process_match_token(char *token, void *userdata)
{
#define THROW(msg, ...)                                                     \
    do {                                                                    \
        log_error("stream %s match for header %s: " msg,                    \
                stst->stream, stst->current_header->name, ##__VA_ARGS__);   \
        return false;                                                       \
    } while (0)

    struct StageState *stst = (struct StageState *)userdata;

    if (stst->current_header) {
        // parse the match
        char *key, *val;
        if (parse_assignment(token, &key, &val)) {
            const struct ProtocolField *f =  protocol_get_field_by_name(stst->current_header->id, key);
            if (f == NULL) {
                THROW("invalid field %s", key);
            }

            struct HeaderMatch *newmatch = calloc_struct(HeaderMatch);
            newmatch->next = stst->current_header->matches;
            stst->current_header->matches = newmatch;
            struct HeaderField *hf = new_headerfield(stst->current_idx, f);
            newmatch->field = *hf;
            free(hf);
            newmatch->value = init_value(f->bitoffset, f->bitcount);
            if (!read_constant(&newmatch->value, stst->current_header->id, f->type, val)) {
                THROW("value '%s' doesn't fit into field '%s'", val, key);
            }
            newmatch->comparator = header_get_field_comprator(&newmatch->field, &newmatch->value);
            if(!newmatch->comparator) {
                THROW("can't find comparator function for the '%s' value", key);
            }
        } else {
            THROW("'%s' is not a valid header field match", token);
        }
    } else {
        struct HeaderDescriptor *h = stst->headers;
        unsigned idx = 0;
        while (h && strcmp(h->name, token) != 0) {
            h = h->next;
            idx++;
        }
        if (h == NULL) {
            log_error("stream %s match refers to non-existing header '%s'",
                    stst->stream, token);
            return false;
        }
        stst->current_header = h;
        stst->current_idx = idx;
    }
    return true;
#undef THROW
}

static bool process_match_stage(char *stage, void *userdata)
{
#define THROW(msg, ...)                                                     \
    do {                                                                    \
        log_error("stream %s match '%s': " msg,                             \
                stst->stream, stage, ##__VA_ARGS__);                        \
        return false;                                                       \
    } while (0)

    struct StageState *stst = (struct StageState *)userdata;

    stst->current_header = NULL;
    if (!foreach_tokens(stage, process_match_token, stst)) {
        THROW("invalid match line");
    }

    if (stst->current_header->matches == NULL) {
        THROW("contains no match");
    }

    REVERSE_LIST(stst->current_header->matches);

    return true;
#undef THROW
}

struct HeaderDescriptor *parse_packet_line(const char *stream, char *line)
{
    struct StageState stst = {
        .stream = stream,
        .headers = NULL,
        .current_header = NULL,
        .current_idx = 0,
    };
    if (!foreach_stages(line, process_packet_stage, &stst)) {
        log_error("failed to parse header list for stream %s", stream);
        delete_header_list(stst.headers);
        return NULL;
    }
    if (stst.headers == NULL) {
        log_error("no headers specified for stream %s", stream);
        return NULL;
    }

    REVERSE_LIST(stst.headers);

    return stst.headers;
}


bool parse_match_line(const char *stream, struct HeaderDescriptor *headers, char *line)
{
    struct StageState stst = {
        .stream = stream,
        .headers = headers,
        .current_header = NULL,
        .current_idx = 0,
    };
    if (!foreach_stages(line, process_match_stage, &stst)) {
        log_error("failed to parse match list for stream %s", stream);
        return false;
    }

    return true;
}

void confheaders_print(const struct HeaderDescriptor *headers)
{
    for (const struct HeaderDescriptor *h=headers; h; h=h->next) {
        log_info("  name %s protocol %s", h->name, protocol_type_from_id(h->id));

        if (h->matches) {
            log_info("  matches:");
            for (const struct HeaderMatch *m=h->matches; m; m=m->next) {
                unsigned bytes = DIVCEIL(m->value.bitoffset + m->value.bitcount, 8);
                unsigned char *cst = (unsigned char *)m->value.value;
                char b_str[128];
                unsigned b_off = 0;
                if (log_enabled(INFO)) {
                    for (unsigned i=0; i<bytes; i++) {
                        if (b_off >= sizeof(b_str)) break;
                        b_off += snprintf(b_str+b_off, sizeof(b_str)-b_off, " 0x%.2x", cst[i]);
                    }
                }

                log_info("    field idx %u bitoffset %u bitcount %u value bitoffset %u bitlength %u, bytes%s",
                        m->field.header_idx, m->field.bitoffset, m->field.bitcount,
                        m->value.bitoffset, m->value.bitcount, b_str);
            }
        }
    }
}


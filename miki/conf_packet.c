
#include "conf_packet.h"
#include "conf_utils.h"
#include "header.h"
#include "parsetree.h"
#include "protocol.h"
#include "utils.h"

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct StageState {
    const char *stream;
    struct HeaderDescriptor *headers;
    struct HeaderDescriptor *current_header;
    unsigned current_idx;
};

static bool process_packet_token(char *token, void *userdata)
{
#define THROW(msg, ...)                                             \
    do {                                                            \
        fprintf(stderr, "stream %s header %s: " msg "\n",           \
                stst->stream, stst->headers->name, ##__VA_ARGS__);  \
        return false;                                               \
    } while (0)

    struct StageState *stst = userdata;

    if (stst->headers->name) {
        // here we don't want parameters for the headers
        THROW("invalid extra parameter '%s'", token);
    } else {
        stst->headers->name = strdup(token);
        stst->headers->type = header_type_from_name(token);
        stst->headers->id = protocol_id_from_type(stst->headers->type);
        if (stst->headers->id < 0) {
            THROW("unknown protocol '%s'", stst->headers->type);
        }
    }

    return true;
#undef THROW
}

static bool process_packet_stage(char *stage, void *userdata)
{
#define THROW(msg, ...)                                             \
    do {                                                            \
        fprintf(stderr, "stream %s: " msg "\n",                     \
                stst->stream, ##__VA_ARGS__);                       \
        return false;                                               \
    } while (0)

    struct StageState *stst = userdata;

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
        fprintf(stderr, "stream %s match for header %s: " msg "\n",         \
                stst->stream, stst->current_header->name, ##__VA_ARGS__);   \
        return false;                                                       \
    } while (0)

    struct StageState *stst = userdata;

    if (stst->current_header) {
        // parse the match
        char *key, *val;
        if (parse_assignment(token, &key, &val)) {
            struct ProtocolField *f =  protocol_get_field_by_name(stst->current_header->id, key);
            if (f == NULL) {
                THROW("invalid field %s", key);
            }

            struct HeaderMatch *newmatch = calloc_struct(HeaderMatch);
            newmatch->next = stst->current_header->matches;
            stst->current_header->matches = newmatch;
            struct HeaderField *hf = new_headerfield(stst->current_idx, f);
            newmatch->field = hf;
            newmatch->value.bitoffset = f->bitoffset;
            newmatch->value.bitcount = f->bitcount;
            if (!read_constant(&newmatch->value, f->type, val)) {
                THROW("value '%s' doesn't fit into field '%s'", val, key);
            }
            newmatch->comparator = header_get_field_comprator(f, &newmatch->value);
            if(!newmatch->comparator) {
                THROW("can't find comparator function for the '%s' value", val);
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
            fprintf(stderr, "stream %s match refers to non-existing header '%s'\n",
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
        fprintf(stderr, "stream %s match '%s': " msg "\n",                  \
                stst->stream, stage, ##__VA_ARGS__);                        \
        return false;                                                       \
    } while (0)

    struct StageState *stst = userdata;

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
    };
    if (!foreach_stages(line, process_packet_stage, &stst)) {
        fprintf(stderr, "failed to parse header list for stream %s\n", stream);
        delete_header_list(stst.headers);
        return NULL;
    }
    if (stst.headers == NULL) {
        fprintf(stderr, "no headers specified for stream %s\n", stream);
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
    };
    if (!foreach_stages(line, process_match_stage, &stst)) {
        fprintf(stderr, "failed to parse match list for stream %s\n", stream);
        return false;
    }

    return true;
}

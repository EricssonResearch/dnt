
#include "conf_packet.h"
#include "conf_utils.h"
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
    bool error;
};

static bool process_token(char *token, void *userdata)
{
#define THROW(msg, ...)                                             \
    do {                                                            \
        fprintf(stderr, "stream %s header %s: " msg "\n",           \
                stst->stream, stst->headers->name, ##__VA_ARGS__);  \
        stst->error = true;                                         \
        return false;                                               \
    } while (0)

    struct StageState *stst = userdata;

    if (stst->headers->name) {
        char *key, *val;
        if (parse_assignment(token, &key, &val)) {
            struct ProtocolField *f =  protocol_get_field_by_name(stst->headers->id, key);
            if (f == NULL) {
                THROW("invalid field %s", key);
            }

            struct HeaderMatch *newmatch = calloc_struct(HeaderMatch);
            newmatch->next = stst->headers->matches;
            stst->headers->matches = newmatch;
            newmatch->field = f;
            newmatch->value.bitoffset = f->bitoffset;
            newmatch->value.bitcount = f->bitcount;
            if (!read_constant(&newmatch->value, f->type, val)) {
                THROW("value '%s' doesn't fit into field '%s'", val, key);
            }
        } else {
            THROW("'%s' is not a valid header field match", token);
        }
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

static bool process_stage(char *stage, void *userdata)
{
    //TODO define THROW
    struct StageState *stst = userdata;

    struct HeaderDescriptor *newheader = calloc_struct(HeaderDescriptor);
    newheader->next = stst->headers;
    stst->headers = newheader;

    foreach_tokens(stage, process_token, stst);

    if (stst->headers->name == NULL) {
        //TODO throw exception: no header type in stage
    }
    if (stst->error) {
        //TODO throw exception
    }

    REVERSE_LIST(stst->headers->matches);

    return true;
}

struct HeaderDescriptor *process_packet_line(const char *stream, char *line)
{
    //TODO define THROW
    struct StageState stst = {
        .stream = stream,
        .headers = NULL,
        .error = false
    };
    foreach_stages(line, process_stage, &stst);
    if (stst.headers == NULL) {
        //TODO throw exception: no headers
    }
    if (stst.error) {
        //TODO throw exception
    }

    REVERSE_LIST(stst.headers);

    return stst.headers;
}



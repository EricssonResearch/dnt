
#include "conf_packet.h"
#include "conf_utils.h"
#include "parsetree.h"
#include "protocol.h"
#include "utils.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

//TODO throw exceptions with longjump

struct StageState {
    const char *stream;
    struct HeaderDescriptor *headers;
};


static struct HeaderMatch *match_list_pop(struct HeaderMatch **list)
{
    struct HeaderMatch *ret = *list;
    *list = (*list)->next;
    ret->next = NULL;
    return ret;
}

static struct HeaderMatch *match_list_push(struct HeaderMatch *list, struct HeaderMatch *e)
{
    e->next = list;
    return e;
}

//TODO template <class T> reverse_list(T *list)
static struct HeaderMatch *reverse_match_list(struct HeaderMatch *list)
{
    struct HeaderMatch *newlist = NULL;
    while (list) {
        struct HeaderMatch *e = match_list_pop(&list);
        newlist = match_list_push(newlist, e);
    }
    return newlist;
}

static struct HeaderDescriptor *header_list_pop(struct HeaderDescriptor **list)
{
    struct HeaderDescriptor *ret = *list;
    *list = (*list)->next;
    ret->next = NULL;
    return ret;
}

static struct HeaderDescriptor *header_list_push(struct HeaderDescriptor *list, struct HeaderDescriptor *e)
{
    e->next = list;
    return e;
}

//TODO template <class T> reverse_list(T *list)
static struct HeaderDescriptor *reverse_header_list(struct HeaderDescriptor *list)
{
    struct HeaderDescriptor *newlist = NULL;
    while (list) {
        struct HeaderDescriptor *e = header_list_pop(&list);
        newlist = header_list_push(newlist, e);
    }
    return newlist;
}

static bool process_token(char *token, void *userdata)
{
    struct StageState *stst = userdata;

    if (stst->headers->name) {
        char *key, *val;
        if (parse_assignment(token, &key, &val)) {
            if (!protocol_fieldname_valid(stst->headers->id, key)) {
                //TODO throw exception: invalid field for protocol
            }

            struct HeaderMatch *newmatch = calloc_struct(HeaderMatch);
            newmatch->fieldname = strdup(key);
            newmatch->fieldvalue = strdup(val);
            newmatch->next = stst->headers->matches;
            stst->headers->matches = newmatch;
        } else {
            //TODO throw exception: 'token' is not a valid header field match
        }
    } else {
        stst->headers->name = strdup(token);
        stst->headers->type = header_type_from_name(token);
        stst->headers->id = protocol_id_from_type(stst->headers->type);
        if (stst->headers->id < 0) {
            //TODO throw exception: unknown protocol
        }
    }

    return true;
}

static bool process_stage(char *stage, void *userdata)
{
    struct StageState *stst = userdata;

    struct HeaderDescriptor *newheader = calloc_struct(HeaderDescriptor);
    newheader->next = stst->headers;
    stst->headers = newheader;

    foreach_tokens(stage, process_token, stst);

    if (stst->headers->name == NULL) {
        //TODO throw exception: no header type in stage
    }

    stst->headers->matches = reverse_match_list(stst->headers->matches);

    return true;
}

struct HeaderDescriptor *process_packet_line(const char *stream, char *line)
{
    struct StageState stst = {
        .stream = stream,
        .headers = NULL
    };
    foreach_stages(line, process_stage, &stst);
    //TODO what if we have no headers? is that legal?

    stst.headers = reverse_header_list(stst.headers);

    return stst.headers;
}




#include "conf_packet.h"
#include "conf_utils.h"
#include "protocol.h"
#include "utils.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

//TODO throw exceptions with longjump

struct StageState {
    const char *stream;
    struct ConfHeader *headers;
};


static struct ConfHeaderMatch *match_list_pop(struct ConfHeaderMatch **list)
{
    struct ConfHeaderMatch *ret = *list;
    *list = (*list)->next;
    ret->next = NULL;
    return ret;
}

static struct ConfHeaderMatch *match_list_push(struct ConfHeaderMatch *list, struct ConfHeaderMatch *e)
{
    e->next = list;
    return e;
}

//TODO template <class T> reverse_list(T *list)
static struct ConfHeaderMatch *reverse_match_list(struct ConfHeaderMatch *list)
{
    struct ConfHeaderMatch *newlist = NULL;
    struct ConfHeaderMatch *e;
    while ((e = match_list_pop(&list)) != NULL) {
        newlist = match_list_push(newlist, e);
    }
    return newlist;
}

static struct ConfHeader *header_list_pop(struct ConfHeader **list)
{
    struct ConfHeader *ret = *list;
    *list = (*list)->next;
    ret->next = NULL;
    return ret;
}

static struct ConfHeader *header_list_push(struct ConfHeader *list, struct ConfHeader *e)
{
    e->next = list;
    return e;
}

//TODO template <class T> reverse_list(T *list)
static struct ConfHeader *reverse_header_list(struct ConfHeader *list)
{
    struct ConfHeader *newlist = NULL;
    struct ConfHeader *e;
    while ((e = header_list_pop(&list)) != NULL) {
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

            struct ConfHeaderMatch *newmatch = calloc_struct(ConfHeaderMatch);
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

    struct ConfHeader *newheader = calloc_struct(ConfHeader);
    newheader->next = stst->headers;
    stst->headers = newheader;

    foreach_tokens(stage, process_token, stst);

    if (stst->headers->name == NULL) {
        //TODO throw exception: no header type in stage
    }

    stst->headers->matches = reverse_match_list(stst->headers->matches);

    return true;
}

struct ConfHeader *process_packet(const char *stream, char *line)
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

//TODO struct ConfHeader *delete_header_list(struct ConfHeader *headers) {}

struct ConfHeader *header_list_find_name(struct ConfHeader *headers, const char *name)
{
    struct ConfHeader *h = headers;
    while (h) {
        if (strcmp(h->name, name) == 0)
            return h;
        h = h->next;
    }
    return NULL;
}

struct ConfHeader *header_list_find_typeid(struct ConfHeader *headers, int id)
{
    struct ConfHeader *h = headers;
    while (h) {
        if (h->id == id)
            return h;
        h = h->next;
    }
    return NULL;
}

char *header_type_from_name(const char *name)
{
    char *under = strchr(name, '_');
    if (under) {
        return strndup(name, under-name);
    } else {
        return strdup(name);
    }
}


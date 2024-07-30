// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "json.h"
#include "object.h"
#include "pipeline.h"
#include "replicate.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>

struct Replicate {
    struct PipelineObject base;
    struct PipelineList *pipes;

    unsigned packets_passed;
    unsigned octets_passed;
};

static struct JsonValue *get_state_json(const struct PipelineObject *obj)
{
    const struct Replicate *rep = (struct Replicate *)obj;
    struct JsonValue *js = json_object();
    json_object_insert(js, "type", json_string("replicate"));
    json_object_insert(js, "name", json_string(obj->name));
    json_object_insert(js, "packets_passed", json_number(rep->packets_passed));
    json_object_insert(js, "octets_passed", json_number(rep->octets_passed));
    return js;
}

struct PipelineList *replicate_get_pipes(struct PipelineObject *rep)
{
    if (rep) {
        struct Replicate *r = (struct Replicate *) rep;
        return r->pipes;
    }
    return NULL;
}

char *repl_sprintf_state_json(struct JsonValue *json, const char *record_sep, const char *line_sep)
{
    (void)record_sep;
    (void)line_sep;
    struct JsonValue *p_pass = json_object_get_number(json, "packets_passed");
    struct JsonValue *o_pass = json_object_get_number(json, "octets_passed");

    if (p_pass && o_pass) {
        return strdup_printf("packets_passed %.0f octets_passed %.0f",
                p_pass->v.number, o_pass->v.number);
    } else {
        return strdup("<invalid replicate state>");
    }
}

struct PipelineObject *new_replicate(const char *name)
{
    struct Replicate *ret = calloc_struct(Replicate);
    ret->base.type = PO_REPL;
    ret->base.name = strdup(name);
    ret->base.process_packet = replicate_packet_passed;
    ret->base.get_state = get_state_json;
    ret->base.reference_count = 1;
    return (struct PipelineObject *)ret;
}

struct PipelineObject *delete_replicate(struct PipelineObject *rep)
{
    free(rep->name);
    free(rep);
    return NULL;
}

enum ActionResult replicate_packet_passed(struct PipelineObject *rep, struct PipelineIterator *pi)
{
    struct Replicate *r = (struct Replicate *)rep;
    __atomic_fetch_add(&r->packets_passed, 1, __ATOMIC_RELAXED);
    //TODO this is not correct if a header was added and then deleted, but
    //      summing the header lengths would be too slow
    __atomic_fetch_add(&r->octets_passed, pi->packet->len + pi->packet->scratch_len, __ATOMIC_RELAXED);
    return ACR_CONTINUE;
}

void store_replication_pipelines(struct PipelineObject *obj, struct PipelineList *pipes)
{
    struct Replicate *r = (struct Replicate *)obj;
    r->pipes = pipes;
}

int try_set_mask(struct PipelineObject *obj, void *userdata)
{
    bool success = false;
    struct MaskArg *args = (struct MaskArg *)userdata;

    if (obj->type == PO_REPL) {
        struct Replicate *r = (struct Replicate *)obj;
        struct PipelineList *list = r->pipes;
        while (list) {
            if (!strcmp(list->pipe->name, args->pipename)) {
                list->pipe->mask = args->new_mask;
                success = true;
                break;
            }
            list = list->next;
        }
    }

    // ...assuming replication:pipeline 1:1
    if (success) {
        args->success = true;
        return 0;
    }
    return 1;
}


// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.

#define OBJECT_INTERNAL

#include "replicate.h"
#include "action.h"
#include "json.h"
#include "notification.h"
#include "object.h"
#include "packet.h"
#include "pipeline.h"
#include "state.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>

struct Replicate {
    struct PipelineObject base;
    struct PipelineList *pipes;

    unsigned long long packets_passed;
    unsigned long long octets_passed;
};

static struct JsonValue *repl_get_state_json(const struct PipelineObject *obj)
{
    const struct Replicate *rep = (struct Replicate *)obj;
    struct JsonValue *js = json_object();
    json_object_insert(js, "type", json_string("replicate"));
    json_object_insert(js, "name", json_string(obj->name));
    json_object_insert(js, "packets_passed", json_number(rep->packets_passed));
    json_object_insert(js, "octets_passed", json_number(rep->octets_passed));
    struct JsonValue *pipe_states = json_array();
    struct PipelineList *iter = rep->pipes;
    while (iter) {
        struct JsonValue *pstate = pipe_get_state(iter->pipe);
        json_array_push(pipe_states, pstate);
        iter = iter->next;
    }
    json_object_insert(js, "pipelines", pipe_states);
    return js;
}

static void repl_print_info(const struct PipelineObject *self, FILE *cmd_w)
{
    const struct Replicate *rep = (struct Replicate *)self;

    fprintf(cmd_w, "    sent %llu packets %llu octets\n",
            rep->packets_passed, rep->octets_passed);
    //TODO also print pipeline state
}

static NotificationLevel repl_notification_pull_fn(void *self, struct JsonValue **msg)
{
    struct PipelineObject *rep = (struct PipelineObject *)self;
    struct JsonValue *js = repl_get_state_json(rep);
    *msg = js;
    return NOTIF_PULL;
}

struct PipelineList *replicate_get_pipes(struct PipelineObject *rep)
{
    if (rep->type == PO_REPL) {
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

static enum ActionResult replicate_packet_passed(struct PipelineObject *rep, struct PipelineIterator *pi)
{
    struct Replicate *r = (struct Replicate *)rep;
    __atomic_fetch_add(&r->packets_passed, 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&r->octets_passed, packet_length(pi->packet), __ATOMIC_RELAXED);
    return ACR_CONTINUE;
}

struct PipelineObject *new_replicate(const char *name)
{
    struct Replicate *ret = calloc_struct(Replicate);
    ret->base.type = PO_REPL;
    ret->base.name = strdup(name);
    ret->base.process_packet = replicate_packet_passed;
    ret->base.get_state = repl_get_state_json;
    ret->base.print_info = repl_print_info;
    ret->base.reference_count = 1;
    notification_register_source(name, repl_notification_pull_fn, ret, 2000);
    return (struct PipelineObject *)ret;
}

struct PipelineObject *delete_replicate(struct PipelineObject *rep)
{
    notification_register_source(rep->name, NULL, NULL, 2000);
    free(rep->name);
    free(rep);
    return NULL;
}

//TODO if Repl actions share their state object this might get mixed up
void store_replication_pipelines(struct PipelineObject *obj, struct PipelineList *pipes)
{
    struct Replicate *r = (struct Replicate *)obj;
    r->pipes = pipes;
}

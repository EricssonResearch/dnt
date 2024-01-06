// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "replicate.h"
#include "json.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>

struct Replicate {
    struct PipelineObject base;

    unsigned packets_passed;
    //TODO count octets (uint64_t)
};

static struct JsonValue *get_state_json(const struct PipelineObject *obj)
{
    const struct Replicate *rep = (struct Replicate *)obj;
    struct JsonValue *js = json_object();
    json_object_insert(js, "type", json_string("replicate"));
    json_object_insert(js, "name", json_string(obj->name));
    json_object_insert(js, "packets_passed", json_number(rep->packets_passed));
    return js;
}

struct PipelineObject *new_replicate(const char *name)
{
    struct Replicate *ret = calloc_struct(Replicate);
    ret->base.type = PO_REPL;
    ret->base.name = strdup(name);
    ret->base.get_state = get_state_json;
    return (struct PipelineObject *)ret;
}

struct PipelineObject *delete_replicate(struct PipelineObject *rep)
{
    free(rep->name);
    free(rep);
    return NULL;
}

void replicate_packet_passed(struct PipelineObject *rep, struct Packet *p)
{
    (void)p;
    struct Replicate *r = (struct Replicate *)rep;
    __atomic_fetch_add(&r->packets_passed, 1, __ATOMIC_RELAXED);
}



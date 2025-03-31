// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "state.h"
#include "conf_actions.h"
#include "conf_interface.h"
#include "conf_streams.h"
#include "interface.h"
#include "log.h"
#include "notification.h"
#include "oam.h"
#include "object.h"
#include "pipeline.h"
#include "utils.h"

#include <string.h>
#include <stdlib.h>

DEFAULT_LOGGING_MODULE(STATE, INFO);


static struct HashMap *state_interfaces = NULL;
static struct HashMap *state_objects = NULL;

static int iface_delete_cb(const char *key, void *value, void *userdata)
{
    (void)key; // owned by the interface
    (void)userdata;
    struct Interface *iface = (struct Interface *)value;
    iface_unref(iface);
    return 1;
}

static int obj_delete_cb(const char *key, void *value, void *userdata)
{
    (void)key; // this is obj->name
    struct PipelineObject *obj = (struct PipelineObject *)value;
    (void)userdata;
    pipeline_object_unref(obj);
    return 1;
}

static int stream_delete_cb(const char *key, void *value, void *userdata)
{
    (void)userdata;
    struct ConfStream *stream = (struct ConfStream *)value;
    delete_confstream(stream);
    free((char*)key);
    return 1;
}

static int iface_stream_delete_cb(const char *key, void *value, void *userdata)
{
    (void)userdata;
    free((char*)key);
    struct ConfStreamList *list = (struct ConfStreamList *)value;
    while (list) {
        struct ConfStreamList *del = list;
        list = list->next;
        free(del->stream_name);
        free(del);
    }
    return 1;
}

static void __attribute__((constructor)) init_state(void)
{
    state_interfaces = new_hashmap(11, iface_delete_cb, NULL);
    state_objects = new_hashmap(11, obj_delete_cb, NULL);
}

static void __attribute__((destructor)) finish_state(void)
{
    delete_hashmap(state_interfaces);
    delete_hashmap(state_objects);
    //TODO we may need to wait for things to finish what they are doing
}


struct StateTransaction *new_transaction(const char *name)
{
    struct StateTransaction *ret = calloc_struct(StateTransaction);
    ret->name = strdup(name);
    ret->ifaces = new_hashmap(11, iface_delete_cb, NULL);
    ret->iface_streams = new_hashmap(11, iface_stream_delete_cb, NULL);
    ret->objects = new_hashmap(11, obj_delete_cb, NULL);
    ret->streams = new_hashmap(22, stream_delete_cb, NULL);
    ret->oam = new_hashmap(7, NULL, NULL);
    ret->del_ifaces = new_hashmap(7, NULL, NULL);
    return ret;
}

struct StateTransaction *delete_transaction(struct StateTransaction *tr)
{
    if (!tr) return NULL;

    free(tr->name);
    //TODO is the order important here?
    delete_hashmap(tr->streams);
    delete_hashmap(tr->objects);
    delete_hashmap(tr->iface_streams);
    delete_hashmap(tr->oam);
    delete_hashmap(tr->ifaces);
    delete_hashmap(tr->del_ifaces);
    free(tr);

    return NULL;
}

static int del_interfaces_cb(const char *key, void *value, void *userdata)
{
    (void)value;
    const char *trname = (const char*)userdata;
    //TODO validate the whole transaction before committing anything
    if (!hashmap_find(state_interfaces, key)) {
        log_error("transaction '%s' del_iface '%s' not found", trname, key);
        return 0;
    }
    hashmap_remove(state_interfaces, key);
    return 1;
}

static int add_new_objects_cb(const char *key, void *value, void *userdata)
{
    (void)key;
    (void)userdata;
    struct PipelineObject *obj = (struct PipelineObject *)value;
    //TODO do we error check here or when we add items to the transaction?
    pipeline_object_ref(obj);
    hashmap_insert(state_objects, obj->name, obj);
    return 1;
}

static int add_new_ifaces_cb(const char *key, void *value, void *userdata)
{
    (void)key;
    (void)userdata;
    struct Interface *iface = (struct Interface *)value;
    //TODO do we error check here or when we add items to the transaction?
    iface_ref(iface);
    hashmap_insert(state_interfaces, iface->name, iface);
    return 1;
}

static int open_new_ifaces_cb(const char *key, void *value, void *userdata)
{
    (void)userdata;
    log_debug("opening interface %s", key);
    struct Interface *iface = (struct Interface *)value;
    return iface->open(iface);
}

struct AddstreamState {
    struct HashMap *pipe_cache;
};

static int addstream_cb(const char *key, void *value, void *userdata)
{
    struct AddstreamState *state = (struct AddstreamState *)userdata;
    struct ConfStreamList *streamlist = (struct ConfStreamList *)value;

    struct Interface *iface = (struct Interface *)hashmap_find(state_interfaces, key);
    if (iface == NULL) {
        log_error("adding streams to interfaces: unknown interface '%s'", key);
        return 0;
    }

    for (struct ConfStreamList *s=streamlist; s; s=s->next) {
        log_info("adding stream %s to interface %s", s->stream_name, key);

        struct Pipeline *pipe = (struct Pipeline *)hashmap_find(state->pipe_cache, s->stream_name);
        if (pipe) {
            log_info("  reusing already compiled pipeline");
        } else {
            log_info("  compiling new pipeline");
            pipe = assemble_actions(s->stream_name, s->stream->actions);
            if (!pipe) {
                log_error("failed to create action pipeline for stream %s", s->stream_name);
                return 0;
            }
            hashmap_insert(state->pipe_cache, s->stream_name, pipe);
        }

        if (!iface_add_stream(iface, s->stream->headers, pipe)) {
            log_error("failed to add stream %s to interface %s",
                    s->stream_name, key);
            return 0;
        }
    }

    return 1;
}

static int pipe_cache_delete_cb(const char *key, void *value, void *userdata)
{
    (void)userdata;
    (void)key; // owned by the pipe
    struct Pipeline *pipe = (struct Pipeline *)value;
    pipeline_unref(pipe);
    return 1;
}

static bool add_streams_to_interfaces(struct StateTransaction *tr)
{
    struct AddstreamState state = {
        .pipe_cache = new_hashmap(29, pipe_cache_delete_cb, NULL),
    };
    if (!hashmap_foreach(tr->iface_streams, addstream_cb, &state)) {
        log_error("failed to add streams to interfaces");
        delete_hashmap(state.pipe_cache);
        return false;
    }
    delete_hashmap(state.pipe_cache);

    return true;
}

static int purge_unused_cb(const char *key, void *value, void *userdata)
{
    struct HashMap *objects = (struct HashMap *)userdata;
    struct PipelineObject *obj = (struct PipelineObject *)value;
    if (obj->reference_count == 1) {
        // this means only the hashmap holds reference
        hashmap_remove(objects, key);
    }
    return 1;
}

static int start_oam_ping_cb(const char *key, void *value, void *userdata)
{
    (void)userdata;
    char *command = (char*)value;
    return oam_start_background_ping(key, command);
}


struct Interface *state_get_interface(const char *ifname)
{
    return (struct Interface *)hashmap_find(state_interfaces, ifname);
}

struct PipelineObject *state_get_object(const char *objname)
{
    return (struct PipelineObject *)hashmap_find(state_objects, objname);
}

struct ForeachIfState {
    state_foreach_if_cb *cb;
    void *userdata;
};
static int foreach_if_cb(const char *key, void *value, void *userdata)
{
    (void)key;
    struct Interface *iface = (struct Interface *)value;
    struct ForeachIfState *st = (struct ForeachIfState *)userdata;
    return st->cb(iface, st->userdata);
}
int state_foreach_interfaces(state_foreach_if_cb *cb, void *userdata)
{
    struct ForeachIfState st = {cb, userdata};
    return hashmap_foreach(state_interfaces, foreach_if_cb, &st);
}

struct ForeachObjState {
    state_foreach_obj_cb *cb;
    void *userdata;
};
static int foreach_obj_cb(const char *key, void *value, void *userdata)
{
    (void)key;
    struct PipelineObject *obj = (struct PipelineObject *)value;
    struct ForeachObjState *st = (struct ForeachObjState *)userdata;
    return st->cb(obj, st->userdata);
}
int state_foreach_objects(state_foreach_obj_cb *cb, void *userdata)
{
    struct ForeachObjState st = {cb, userdata};
    return hashmap_foreach(state_objects, foreach_obj_cb, &st);
}

bool state_commit_transaction(struct StateTransaction *tr)
{
    log_info("committing transaction '%s' ...", tr->name);
    // note: the order of operations is important

    //TODO remove streams that are on the del list (no such list yet)

    if (!hashmap_foreach(tr->del_ifaces, del_interfaces_cb, tr->name)) {
        //TODO rollback
        return false;
    }

    if (!hashmap_foreach(tr->objects, add_new_objects_cb, NULL)) {
        //TODO rollback
        return false;
    }

    if (!hashmap_foreach(tr->ifaces, add_new_ifaces_cb, NULL)) {
        //TODO rollback
        return false;
    }

    if (!add_streams_to_interfaces(tr)) {
        //TODO rollback
        return false;
    }

    if (!hashmap_foreach(tr->ifaces, open_new_ifaces_cb, NULL)) {
        //TODO rollback
        return false;
    }

    if (!hashmap_foreach(state_objects, purge_unused_cb, state_objects)) {
        //TODO rollback
        return false;
    }

    if (!hashmap_foreach(tr->oam, start_oam_ping_cb, NULL)) {
        //TODO rollback
        return false;
    }

    struct JsonValue *msg = json_object();
    json_object_insert(msg, "committed", json_string(tr->name));
    notification_push_event("transaction", NOTIF_INFO, msg);
    log_info("transaction '%s' committed successfully", tr->name);

    return true;
}

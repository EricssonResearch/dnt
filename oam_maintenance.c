// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#define OAM_INTERNAL

#include "oam.h"
#include "oam_maintenance.h"

#include "hashmap.h"
#include "log.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>

DEFAULT_LOGGING_MODULE(OAM, INFO);

struct MP_MPLS_Address {
    unsigned label;
};

struct MP_ETH_Address {
    unsigned short vlan;
    //TODO mac, vlan
};

struct OAM_MaintenancePoint {
    char *name;
    char *stream_name;
    enum OAM_MP_Type type;
    unsigned level;

    int reference_count;

    struct Pipeline *pipe;
    int pipe_pos_idx;
    //TODO address for injection

    struct PipelineObject *object;

    unsigned oam_send;
    unsigned oam_recv;
};

static struct HashMap *mp_hash = NULL; // name -> struct OAM_MaintenancePoint


static int mp_delete_cb(const char *key, void *value, void *userdata)
{
    (void)key;
    (void)userdata;
    struct OAM_MaintenancePoint *mp = (struct OAM_MaintenancePoint *)value;

    //TODO unregister notification
    if (mp->object)
        pipeline_object_unref(mp->object);
    free(mp->name);
    free(mp->stream_name);
    free(mp);

    return 1;
}

struct OAM_MaintenancePoint *new_maintenance_point(const char *stream_name, const char *mp_name,
        enum OAM_MP_Type type, unsigned level,
        struct PipelineObject *obj, struct Pipeline *pipe, unsigned idx)
{
    struct OAM_MaintenancePoint *mp = NULL;
    if (mp_hash == NULL) {
        mp_hash = new_hashmap(13, mp_delete_cb, NULL);
    } else {
        mp = (struct OAM_MaintenancePoint *)hashmap_find(mp_hash, mp_name);
    }

    if (mp) {
        if (mp->object != obj) {
            if (level != mp->level) {
                log_error("Redefined MP '%s' with level %u previous level %u",
                        mp_name, level, mp->level);
                return NULL;
            }

            if (type != mp->type) {
                log_error("Redefined MP '%s' with type %s previous type %s",
                        mp_name, mp_type_to_str(type), mp_type_to_str(mp->type));
                return NULL;
            }

            if (!obj) {
                log_error("Redefined MP '%s' without object, original object is '%s'",
                        mp_name, pipelineobject_get_name(mp->object));
            } else if (!mp->object) {
                log_error("Redefined MP '%s' with object '%s' conflict with previous definition without object",
                        mp_name, pipelineobject_get_name(obj));
            } else {
                log_error("Redefined MP '%s' with object '%s' previous object '%s'",
                        mp_name, pipelineobject_get_name(obj), pipelineobject_get_name(mp->object));
            }
            return NULL;
        }

        if (strcmp(stream_name, mp->stream_name) != 0) {
            log_error("MP '%s' defined twice, in streams '%s' and '%s'",
                    mp_name, mp->stream_name, stream_name);
            return NULL;
        }

        int refcount = __atomic_fetch_add(&mp->reference_count, 1, __ATOMIC_RELAXED);
        log_debug("%s ref refcount %d", mp_name, refcount);
        return mp;
    }

    if (type != OAM_Stop) {
        if (pipe == NULL) {
            log_error("%s '%s' needs a pipeline injection point",
                    mp_type_to_str(type), mp_name);
            return NULL;
        }
    }

    mp = calloc_struct(OAM_MaintenancePoint);
    mp->name = strdup(mp_name);
    mp->stream_name = strdup(stream_name);
    mp->type = type;
    mp->level = level;

    mp->reference_count = 1;

    mp->pipe = pipe;
    mp->pipe_pos_idx = idx;

    mp->object = obj;
    if (obj) {
        pipeline_object_ref(obj);
        pipelineobject_store_mep_start_name(obj, mp_name);
    }

    hashmap_insert(mp_hash, mp->name, mp);
    //TODO notification_register_source

    log_debug("%s create refcount 1", mp_name);

    return mp;
}

void unref_maintenance_point(struct OAM_MaintenancePoint *mp)
{
    int refcount = __atomic_sub_fetch(&mp->reference_count, 1, __ATOMIC_RELAXED);
    log_debug("%s unref refcount %d", mp->name, refcount);

    if (refcount == 0) {
        hashmap_remove(mp_hash, mp->name);
    }
}

const char *mp_type_to_str(enum OAM_MP_Type type)
{
    switch (type) {
        case  OAM_Start:
            return "MEP-Start";
        case OAM_Stop:
            return "MEP-Stop";
        case OAM_Intermediate:
            return "MIP";
    }
    return NULL;
}

enum OAM_MP_Type mp_get_type(struct OAM_MaintenancePoint *mp)
{
    return mp->type;
}

struct JsonValue *mp_get_state_json(struct OAM_MaintenancePoint *mp, int object_info)
{
    struct JsonValue *ret = json_object();
    json_object_insert(ret, "name", json_string(mp->name));
    json_object_insert(ret, "stream_name", json_string(mp->stream_name));
    json_object_insert(ret, "type", json_string(mp_type_to_str(mp->type)));
    json_object_insert(ret, "level", json_number(mp->level));
    json_object_insert(ret, "send", json_number(mp->oam_send));
    json_object_insert(ret, "recv", json_number(mp->oam_recv));

    if (mp->object) {
        if (object_info) {
            struct JsonValue *objinfo = mp->object->get_state(mp->object);
            json_object_insert(ret, "object", objinfo);
        } else {
            json_object_insert(ret, "object", json_string(pipelineobject_get_name(mp->object)));
        }
    }

    //TODO add mask state

    return ret;
}

void mp_inject_packet(struct OAM_MaintenancePoint *mp, struct Packet *packet)
{
    //TODO when do we set the addressing?

    struct PipelineIterator *pi = new_pipe_iterator(mp->pipe, packet);
    pi->pos = mp->pipe_pos_idx;
    pipe_iterator_run(pi);
    __atomic_fetch_add(&mp->oam_send, 1, __ATOMIC_RELAXED);
}

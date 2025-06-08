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
    enum OAM_MP_Flavor flavor;
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

static bool reinterpret_pw_packet(struct Packet *p)
{
    // we must have at least mpls, dcw
    // TODO can we have more than one mpls label?
    if (p->header_count < 2) {
        log_error("OAM packet doesn't have 2 identified headers (mpls, dcw), how was this matched??");
        return false;
    }

    if (p->headers[1].type != PROTO_ID_OAM) {
        for (unsigned i=1; i<p->header_count-1; i++) {
            if (p->headers[i+1].start != p->headers[i].start + p->headers[i].len) {
                log_error("OAM packet is not continuous in memory at header %u type %s",
                        i, protocol_type_from_id(p->headers[i].type));
                return false;
            }
        }

        unsigned plen = packet_length(p);
        p->headers[1].type = PROTO_ID_OAM;
        p->headers[1].len = 8; // length of oam
        p->headers[2].type = PROTO_ID_PAYLOAD;
        p->headers[2].start = p->headers[1].start + 8;
        p->headers[2].len = plen - 4 - 8; // length of mpls and oam
        p->header_count = 3;
    }
    return true;
}

static struct JsonValue *unpack_pw_message(const struct Packet *p)
{
    // we know the packet has at least two headers (mpls, dcw)
    for (unsigned i=1; i<p->header_count-1; i++) {
        if (p->headers[i+1].start != p->headers[i].start + p->headers[i].len) {
            log_error("Received PW OAM packet is not continuous in memory at header %u type %s",
                    i, protocol_type_from_id(p->headers[i].type));
            return NULL;
        }
    }

    unsigned plen = packet_length(p);
    if (plen < 4 + 8) { // mpls + d-ACH
        log_error("PW OAM packet is too short");
        return NULL;
    }

    unsigned char *dach_start = p->buf + p->headers[1].start;
    char *json_str = (char*)(p->buf + p->headers[1].start + 8);
    unsigned json_len = plen - 4 - 8;

    INTERPRET_DACH(dach_start);

    char *jerror;
    struct JsonValue *js = json_parse(json_str, json_len, &jerror);
    if (js == NULL || js->type != JSON_OBJECT) {
        log_error("Received PW OAM packet contains invalid JSON string: %s", jerror);
        free(jerror);
        return NULL;
    }

#define ADD_DACH_FIELD(_name)                                               \
    do {                                                                    \
        struct JsonValue *v = json_object_get_any(js, #_name);              \
        if (v) {                                                            \
            log_warning("Received PW OAM packet's JSON contains " #_name);  \
        }                                                                   \
        json_object_insert(js, #_name, json_number(dach._name));            \
    } while (0)

    ADD_DACH_FIELD(version);
    ADD_DACH_FIELD(seq);
    ADD_DACH_FIELD(channel);
    ADD_DACH_FIELD(nodeid);
    ADD_DACH_FIELD(level);
    ADD_DACH_FIELD(flags);
    ADD_DACH_FIELD(session);

#undef ADD_DACH_FIELD

    return js;
}

static bool update_pw_payload(struct Packet *p, const struct JsonValue *msg)
{
    // note: currently we only use this when doing ping's route request

    struct JsonValue *out = json_duplicate(msg);
    const char *ach_keys[] = {"version", "seq", "channel", "nodeid", "level", "flags", "session" };
    for (unsigned i=0; i<ARRAY_SIZE(ach_keys); i++) {
        json_object_remove(out, ach_keys[i]);
    }

    unsigned js_length;
    char *js_string = json_serialize(out, &js_length);
    if (js_string == NULL) {
        log_error("could not serialize the updated message");
        json_delete(out);
        return false;
    }

    // write the new json string into p
    // we expect that mp_reinterpret_oam_packet() has been called on the packet
    // TODO this packet manipulation is extremely ugly
    // TODO what if we have more than 1 mpls label?
    char *payload = (char *)(p->buf + p->headers[2].start);
    memcpy(payload, js_string, js_length);
    free(js_string);
    if (p->headers[2].start < p->start) {
        // it is on the scratch
        // note: in mp_reinterpret_oam_packet() we've verified that the headers are
        //       continuous in memory, so it's safe to assume that the end of
        //       p->headers[2] is the end of the scratch space
        p->scratch_len = p->headers[2].start + js_length;
    } else {
        // it is in the receive area
        p->len = 4 + 8 + js_length;
    }
    p->headers[2].len = js_length;
    return true;
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

        int refcount = __atomic_add_fetch(&mp->reference_count, 1, __ATOMIC_RELAXED);
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
    mp->flavor = OAM_PW; //TODO support other flavors
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

const char *mp_get_name(const struct OAM_MaintenancePoint *mp)
{
    return mp->name;
}

const char *mp_get_stream_name(const struct OAM_MaintenancePoint *mp)
{
    return mp->stream_name;
}

enum OAM_MP_Type mp_get_type(const struct OAM_MaintenancePoint *mp)
{
    return mp->type;
}

struct JsonValue *mp_get_state_json(const struct OAM_MaintenancePoint *mp, int object_info)
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

struct AddMPState {
    struct JsonValue *jlist;
    struct PipelineObject *object;
};
static int add_mp_cb(const char *key, void *value, void *userdata)
{
    (void)key;
    struct AddMPState *st = (struct AddMPState *)userdata;
    struct OAM_MaintenancePoint *mp = (struct OAM_MaintenancePoint *)value;

    if (mp->object == st->object) {
        json_array_push(st->jlist, mp_get_state_json(mp, 0));
    }
    return 1;
}

struct JsonValue *mp_get_state_json_by_object(const struct OAM_MaintenancePoint *mp)
{
    struct JsonValue *jlist = json_array();

    if (mp->object) {
        json_array_push(jlist, mp_get_state_json(mp, 0));
    } else {
        struct AddMPState st = { jlist, mp->object };
        foreach_mp(add_mp_cb, &st);
    }

    return jlist;
}

int foreach_mp(hashmap_cb *cb, void *userdata)
{
    return hashmap_foreach(mp_hash, cb, userdata);
}

bool mp_reinterpret_oam_packet(struct OAM_MaintenancePoint *mp, struct Packet *p)
{
    if (mp->flavor == OAM_PW) {
        return reinterpret_pw_packet(p);
    }
    //TODO other flavors
    return false;
}

void mp_count_received_message(struct OAM_MaintenancePoint *mp, const struct Packet *p)
{
    (void)p;
    __atomic_add_fetch(&mp->oam_recv, 1, __ATOMIC_RELAXED);
}

struct JsonValue *mp_unpack_message(const struct OAM_MaintenancePoint *mp, const struct Packet *p)
{
    if (mp->flavor == OAM_PW) {
        return unpack_pw_message(p);
    }
    //TODO unpack other flavors

    return NULL;
}

bool mp_update_message_payload(const struct OAM_MaintenancePoint *mp, struct Packet *p, const struct JsonValue *msg)
{
    if (mp->flavor == OAM_PW) {
        return update_pw_payload(p, msg);
    }
    //TODO other flavors

    return false;
}

int mp_compare_level(const struct OAM_MaintenancePoint *mp, unsigned level)
{
    return (int)level - (int)mp->level;
}

void mp_inject_packet(struct OAM_MaintenancePoint *mp, struct Packet *p)
{
    //TODO when do we set the addressing?

    __atomic_fetch_add(&mp->oam_send, 1, __ATOMIC_RELAXED);
    struct PipelineIterator *pi = new_pipe_iterator(mp->pipe, p);
    //TODO pipe_iterator_inject_at(pi, mp->pipe_pos_idx);
    pi->pos = mp->pipe_pos_idx;
    pipe_iterator_run(pi);
}


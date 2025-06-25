// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#define OAM_INTERNAL

#include "oam_maintenance.h"

#include "hashmap.h"
#include "log.h"
#include "notification.h"
#include "oam.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>

DEFAULT_LOGGING_MODULE(OAM, INFO);

#define OAM_CHANNEL 0x7fff /* Experimental channel type, we are not compatible with anything */

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
    enum OAM_MP_Encap encap;
    unsigned level;

    int reference_count;

    struct Pipeline *pipe;
    int pipe_pos_idx;

    const enum ProtocolID *protostack;
    //TODO address for injection

    struct PipelineObject *object;

    unsigned oam_send;
    unsigned oam_recv;
};

// note: this hash doesn't hold reference to the MPs in it
static struct HashMap *mp_hash = NULL; // name -> struct OAM_MaintenancePoint


static int mp_delete_cb(const char *key, void *value, void *userdata)
{
    (void)userdata;
    struct OAM_MaintenancePoint *mp = (struct OAM_MaintenancePoint *)value;

    notification_register_source(key, NULL, NULL, 0);
    if (mp->object)
        pipeline_object_unref(mp->object);
    free(mp->name);
    free(mp->stream_name);
    free(mp);

    return 1;
}

static NotificationLevel mp_notification_pull_fn(void *self, struct JsonValue **msg)
{
    struct OAM_MaintenancePoint *mp = (struct OAM_MaintenancePoint *)self;
    struct JsonValue *state = mp_get_state_json(mp, true);
    *msg = state;
    return NOTIF_PULL;
}

static struct JsonValue *unpack_pw_message(const struct Packet *p)
{
    if (p->header_count < 2) {
        log_error("OAM packet doesn't have 2 identified headers (mpls, dcw), how was this matched??");
        return NULL;
    }

    if (!packet_is_linear(p)) {
        log_error("OAM packet is not continuous in memory");
        return NULL;
    }

    //TODO support >1 mpls headers?
    //  use protostack

    unsigned plen = packet_length(p);
    unsigned header_len = protocol_from_id(PROTO_ID_MPLS)->bytelength +
        protocol_from_id(PROTO_ID_OAM)->bytelength;

    if (plen < header_len) {
        log_error("PW OAM packet is too short");
        return NULL;
    }

    unsigned char *dach_start = p->buf + p->headers[0].start + protocol_from_id(PROTO_ID_MPLS)->bytelength;
    char *json_str = (char*)(dach_start + protocol_from_id(PROTO_ID_OAM)->bytelength);
    unsigned json_len = plen - header_len;

    //TODO decode mpls label into the json?

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

static struct JsonValue *pack_pw_message_header(struct Packet *p, const struct OamRequest *req)
{
    unsigned channel = OAM_CHANNEL;
    unsigned nodeid;
    unsigned char level;
    unsigned char session;
    unsigned char seq;
    unsigned char ttl;

    request_get_identification_data(req, &nodeid, &level, &session, &seq, &ttl);

    packet_clear_headers(p);
    packet_enlarge_scratch(p);

    packet_add_header(p, 0, PROTO_ID_MPLS, protocol_from_id(PROTO_ID_MPLS)->bytelength);
    packet_add_header(p, 1, PROTO_ID_OAM, protocol_from_id(PROTO_ID_OAM)->bytelength);
    packet_add_header(p, 2, PROTO_ID_PAYLOAD, 0);

    unsigned char *mpls = p->buf + p->headers[0].start;
    mpls[0] = 0; //TODO write label if we have it
    mpls[1] = 0;
    mpls[2] = 1; // BOS
    mpls[3] = ttl;
    unsigned char *oam  = p->buf + p->headers[1].start;
    oam[0] = 0x10; // indicator and version
    oam[1] = seq;
    oam[2] = (channel>>8) & 0xff;
    oam[3] = channel & 0xff;
    oam[4] = (nodeid>>12) & 0xff;
    oam[5] = (nodeid>>4) & 0xff;
    oam[6] = ((nodeid&0xf) << 4) + ((level & 0x07) << 1);
    oam[7] = session & 0x0f;

    p->ttl = ttl;

    struct JsonValue *js = json_object();
    json_object_insert(js, "type", json_string(request_get_type(req)));
    json_object_insert(js, "code", json_string("request"));

    struct timespec sendtime;
    clock_gettime(CLOCK_REALTIME, &sendtime);
    p->recv_time = sendtime;
    timespec_to_tsntstamp(p->timestamp, &sendtime);

    json_object_insert(js, "send_s", json_number(sendtime.tv_sec));
    json_object_insert(js, "send_ns", json_number(sendtime.tv_nsec));

    return js;
}

static bool pack_pw_payload(struct Packet *p, const struct JsonValue *msg)
{
    if (!packet_is_linear(p)) {
        log_error("can't update message in packet that is not continuous in memory");
        return false;
    }

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
    json_delete(out);

    // TODO support >1 mpls label?

    // write the new json string into p
    // TODO this packet manipulation is extremely ugly
    unsigned header_len = protocol_from_id(PROTO_ID_MPLS)->bytelength +
        protocol_from_id(PROTO_ID_OAM)->bytelength;
    char *payload = (char *)(p->buf + p->headers[0].start + header_len);
    memcpy(payload, js_string, js_length);
    free(js_string);
    unsigned new_len = header_len + js_length;
    // note: we don't know how many and what type of headers there are in @p
    //       we only know that the packet is linear in memory
    if (p->headers[0].start < p->start) {
        // data is on the scratch
        p->headers[p->header_count-1].len += new_len - p->scratch_len;
        p->scratch_len = new_len;
    } else {
        // data is in the receive area
        p->headers[p->header_count-1].len += new_len - p->len;
        p->len = new_len;
    }
    return true;
}

static int compare_pw_level(const struct OAM_MaintenancePoint *mp, const struct Packet *p)
{
    if (p->header_count < 2) {
        log_error("OAM packet doesn't have 2 identified headers (mpls, dcw), how was this matched??");
        return -1;
    }

    if (!packet_is_linear(p)) {
        log_error("OAM packet is not continuous in memory");
        return -1;
    }

    //TODO support >1 mpls headers?
    //  use protostack

    unsigned plen = packet_length(p);
    unsigned header_len = protocol_from_id(PROTO_ID_MPLS)->bytelength +
        protocol_from_id(PROTO_ID_OAM)->bytelength;

    if (plen < header_len) {
        log_error("PW OAM packet is too short");
        return -1;
    }

    unsigned char *dach_start = p->buf + p->headers[0].start + protocol_from_id(PROTO_ID_MPLS)->bytelength;

    INTERPRET_DACH(dach_start);

    return (int)dach.level - (int)mp->level;
}

static unsigned char get_pw_ttl(const struct Packet *p)
{
    if (p->header_count < 2) {
        log_error("OAM packet doesn't have 2 identified headers (mpls, dcw), how was this matched??");
        return -1;
    }

    unsigned char *mpls_start = p->buf + p->headers[0].start;
    return mpls_start[3];
}


struct OAM_MaintenancePoint *oam_new_maintenance_point(const char *stream_name, const char *mp_name,
        enum OAM_MP_Type type, unsigned level,
        const enum ProtocolID *protostack,
        struct PipelineObject *obj, struct Pipeline *pipe, unsigned idx,
        struct OAM_MP_Address *addr)
{
    (void)addr; //TODO process this (must copy the contents!)

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
                        mp_name, oam_mp_type_to_str(type), oam_mp_type_to_str(mp->type));
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

        if (pipe != NULL && mp->pipe == NULL) {
            mp->pipe = pipe;
            mp->pipe_pos_idx = idx;
        }
        int refcount = __atomic_add_fetch(&mp->reference_count, 1, __ATOMIC_RELAXED);
        log_debug("%s ref refcount %d", mp_name, refcount);
        return mp;
    }

    //TODO for MIP we first create the receiver that has no pipe pointer
    /*if (type != OAM_Stop) {
        if (pipe == NULL) {
            log_error("%s '%s' needs a pipeline injection point",
                    oam_mp_type_to_str(type), mp_name);
            return NULL;
        }
    }*/

    mp = calloc_struct(OAM_MaintenancePoint);
    mp->name = strdup(mp_name);
    mp->stream_name = strdup(stream_name);
    mp->type = type;
    mp->encap = protostack[0] == PROTO_ID_ETH ? OAM_TSN : OAM_PW;
    mp->level = level;
    mp->protostack = protostack;

    mp->reference_count = 1;

    mp->pipe = pipe;
    mp->pipe_pos_idx = idx;

    mp->object = obj;
    if (obj) {
        pipeline_object_ref(obj);
        pipelineobject_store_mep_start_name(obj, mp_name);
    }

    hashmap_insert(mp_hash, mp->name, mp);
    notification_register_source(mp_name, mp_notification_pull_fn, mp, 2000);

    log_debug("%s create refcount 1", mp_name);

    return mp;
}

void oam_unref_maintenance_point(struct OAM_MaintenancePoint *mp)
{
    int refcount = __atomic_sub_fetch(&mp->reference_count, 1, __ATOMIC_RELAXED);
    log_debug("%s unref refcount %d", mp->name, refcount);

    if (refcount == 0) {
        hashmap_remove(mp_hash, mp->name);
    }

    if (hashmap_count(mp_hash) == 0) {
        mp_hash = delete_hashmap(mp_hash);
    }
}

struct OAM_MaintenancePoint *find_maintenance_point(const char *name)
{
    if (mp_hash) {
        struct OAM_MaintenancePoint *mp = (struct OAM_MaintenancePoint *)hashmap_find(mp_hash, name);
        if (mp) {
            int refcount = __atomic_add_fetch(&mp->reference_count, 1, __ATOMIC_RELAXED);
            log_debug("%s ref refcount %d", name, refcount);
        }
        return mp;
    } else {
        return NULL;
    }
}

const char *oam_mp_type_to_str(enum OAM_MP_Type type)
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

const char *oam_mp_encap_to_str(enum OAM_MP_Encap encap)
{
    switch (encap) {
        case OAM_PW:
            return "PseudoWire";
        case OAM_TSN:
            return "TSN";
        case OAM_SRv6:
            return "SRv6";
    }
    return NULL;
}

const char *oam_mp_addr_source_to_str(enum OAM_MP_Addr_Source src)
{
    switch (src) {
        case OAM_FROM_Unknown:
            return "Unknown";
        case OAM_FROM_Edit:
            return "Edit before MP";
        case OAM_FROM_Match:
            return "Match";
        case OAM_FROM_Later:
            return "Edit after MP";
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

unsigned mp_get_level(const struct OAM_MaintenancePoint *mp)
{
    return mp->level;
}

enum OAM_MP_Type mp_get_type(const struct OAM_MaintenancePoint *mp)
{
    return mp->type;
}

bool mp_can_send(const struct OAM_MaintenancePoint *mp)
{
    if (mp->type == OAM_Stop)
        return false;
    if (mp->pipe == NULL)
        return false;
    //TODO also check if we have the necessary addressing info
    return true;
}

struct JsonValue *mp_get_state_json(const struct OAM_MaintenancePoint *mp, bool object_info)
{
    struct JsonValue *ret = json_object();
    json_object_insert(ret, "name", json_string(mp->name));
    json_object_insert(ret, "stream_name", json_string(mp->stream_name));
    json_object_insert(ret, "type", json_string(oam_mp_type_to_str(mp->type)));
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
        json_array_push(st->jlist, mp_get_state_json(mp, false));
    }
    return 1;
}

struct JsonValue *mp_get_state_json_by_object(const struct OAM_MaintenancePoint *mp)
{
    struct JsonValue *jlist = json_array();

    if (mp->object) {
        json_array_push(jlist, mp_get_state_json(mp, false));
    } else {
        struct AddMPState st = { jlist, mp->object };
        foreach_mp(false, add_mp_cb, &st);
    }

    return jlist;
}

void mp_print_info(const struct OAM_MaintenancePoint *mp, FILE *out, bool details)
{
    fprintf(out, "%s in %s level %u %s",
            mp->name, mp->stream_name, mp->level, oam_mp_encap_to_str(mp->encap));
    if (mp->pipe)
        fprintf(out, " (pipe %s idx %u)", mp->pipe->name, mp->pipe_pos_idx);

    if (details) {
        if (mp->object)
            fprintf(out, "\n    object %s type %s",
                    pipelineobject_get_name(mp->object), pipelineobject_name_from_type(mp->object->type));
        fprintf(out, "\n    send %u recv %u",
                mp->oam_send, mp->oam_recv);
    }
}

int foreach_mp(bool sorted, hashmap_cb *cb, void *userdata)
{
    if (sorted)
        return hashmap_foreach_sorted(mp_hash, cb, userdata);
    else
        return hashmap_foreach(mp_hash, cb, userdata);
}

void mp_count_received_message(struct OAM_MaintenancePoint *mp, const struct Packet *p)
{
    (void)p;
    __atomic_add_fetch(&mp->oam_recv, 1, __ATOMIC_RELAXED);
}

struct JsonValue *mp_unpack_message(const struct OAM_MaintenancePoint *mp, const struct Packet *p)
{
    if (mp->encap == OAM_PW) {
        return unpack_pw_message(p);
    }
    //TODO unpack other encaps

    return NULL;
}

struct JsonValue *mp_pack_message_header(const struct OAM_MaintenancePoint *mp, struct Packet *p, const struct OamRequest *req)
{
    if (mp->encap == OAM_PW) {
        return pack_pw_message_header(p, req);
    }
    //TODO pack other encaps

    return NULL;
}

bool mp_pack_message_payload(const struct OAM_MaintenancePoint *mp, struct Packet *p, const struct JsonValue *msg)
{
    if (mp->encap == OAM_PW) {
        return pack_pw_payload(p, msg);
    }
    //TODO other encaps

    return false;
}

int mp_compare_level(const struct OAM_MaintenancePoint *mp, const struct Packet *p)
{
    if (mp->encap == OAM_PW) {
        return compare_pw_level(mp, p);
    }
    //TODO other encaps

    return -1;
}

unsigned char mp_get_ttl(const struct OAM_MaintenancePoint *mp, const struct Packet *p)
{
    if (mp->encap == OAM_PW) {
        return get_pw_ttl(p);
    }
    if (mp->encap == OAM_TSN) {
        return 255;
    }

    return 0;
}

void mp_inject_packet(struct OAM_MaintenancePoint *mp, struct Packet *p)
{
    //TODO when do we set the addressing?

    if (mp->pipe == NULL) {
        log_error("mp %s can't send without a pipe", mp->name);
        return;
    }
    __atomic_fetch_add(&mp->oam_send, 1, __ATOMIC_RELAXED);
    struct PipelineIterator *pi = new_pipe_iterator(mp->pipe, p);
    //TODO pipe_iterator_inject_at(pi, mp->pipe_pos_idx);
    pi->pos = mp->pipe_pos_idx;
    pipe_iterator_run(pi);
}


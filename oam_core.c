// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#define OAM_INTERNAL

#include "oam.h"
#include "oam_command.h"
#include "oam_core.h"
#include "oam_message.h"
#include "oam_request.h"

#include "if_oam.h"
#include "hashmap.h"
#include "log.h"
#include "notification.h"
#include "object.h"
#include "seq_recov.h"
#include "thread_utils.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>

DEFAULT_LOGGING_MODULE(OAM, INFO);


static struct HashMap *oam_ifaces;
static struct Interface *oam_default_iface = NULL;
static struct Interface *oam_cmd_iface = NULL;

static struct HashMap *mep_starts = NULL; // name -> struct MEPStart

struct StreamNameAssociation {
    struct HashMap *names;
    struct StreamNameAssociation *next;
};

static struct StreamNameAssociation *stream_names = NULL;

static bool oam_initialized = false;

bool set_oam_cmd_if(struct Interface *iface)
{
    if (oam_cmd_iface == NULL) {
        oam_cmd_iface = iface;
        return true;
    } else {
        log_error("only one OAM command interface is supported, config has '%s' and '%s'",
                oam_cmd_iface->name, iface->name);
        return false;
    }
}

static int oam_if_del_cb(const char *key, void *value, void *userdata)
{
    // we don't own the key or the value
    (void)key;
    (void)value;
    (void)userdata;
    return 1;
}

void add_oam_if(struct Interface *iface)
{
    if (oam_ifaces == NULL) {
        oam_ifaces = new_hashmap(9, oam_if_del_cb, NULL);
    }
    hashmap_insert(oam_ifaces, iface->name, iface);

    if (oam_default_iface == NULL) {
        oam_default_iface = iface;
    } else {
        if (strcmp(iface->name, oam_default_iface->name) < 0)
            oam_default_iface = iface;
    }
}

struct Interface *get_oam_interface(const char *ifname)
{
    return ifname[0] ? (struct Interface *)hashmap_find(oam_ifaces, ifname) : oam_default_iface;
}

struct Interface *get_default_oam_interface(void)
{
    return oam_default_iface;
}

int foreach_oam_ifaces(hashmap_cb *cb, void *userdata)
{
    return hashmap_foreach_sorted(oam_ifaces, cb, userdata);
}

bool have_default_iface(void)
{
    return oam_default_iface != NULL;
}

unsigned short get_node_id(void)
{
    return oamif_get_uid(oam_default_iface);
}

static bool is_masked(const struct MepStart *mep, const struct timespec *now);

struct JsonValue *mep_start_get_state(const struct MepStart *mep_start)
{
    struct JsonValue *ret = json_object();
    json_object_insert(ret, "packets_passed", json_number(mep_start->packets_passed));
    json_object_insert(ret, "octets_passed", json_number(mep_start->octets_passed));
    json_object_insert(ret, "oam_packets_passed", json_number(mep_start->oam_packets_passed));
    json_object_insert(ret, "oam_octets_passed", json_number(mep_start->oam_octets_passed));
    json_object_insert(ret, "name", json_string(mep_start->name));
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    bool masked = is_masked(mep_start, &now);
    json_object_insert(ret, "mask_signal_state", json_string(masked ? "masked" : "unmasked"));
    json_object_insert(ret, "type", json_string("mep_state"));
    return ret;
}

static NotificationLevel mep_start_notification_pull_fn(void *self, struct JsonValue **msg)
{
    struct MepStart *mep = (struct MepStart *) self;
    struct JsonValue *state = mep_start_get_state(mep);
    *msg = state;
    return NOTIF_PULL;
}

static int mep_start_delete_cb(const char *key, void *value, void *userdata)
{
    (void)key;
    (void)userdata;
    struct MepStart *mepstart = (struct MepStart *)value;
    notification_register_source(mepstart->name, NULL, NULL, 2000);
    free(mepstart->name);
    free(mepstart->stream_name);
    free(mepstart);
    return 1;
}

struct AddMepState {
    struct JsonValue *jlist;
    struct MepStart *mep;
};
static int addtrig_cb(const char *key, void *value, void *userdata)
{
    (void) key;
    struct AddMepState *st = (struct AddMepState *)userdata;
    struct MepStart *mep = (struct MepStart *)value;

    if (mep_start_in_stream(mep, st->mep->stream_name)) {
        // limit to meps with the same target
        if(mep->target == st->mep->target) {
            struct JsonValue *state = mep_start_get_state(mep);
            json_array_push(st->jlist, state);
        }
    }
    return 1;
}

struct JsonValue *mep_start_get_state_by_target(struct MepStart *mep_start)
{
    struct JsonValue *jlist = json_array();

    if(mep_start) {
        struct AddMepState st = {jlist, mep_start};
        foreach_mep_start(addtrig_cb, &st);
    }
    return jlist;
}

bool oam_create_mep_start(const char *stream_name, const char *mep_name, int level,
        struct PipelineObject *obj, struct Pipeline *pipe, unsigned idx)
{
    if (mep_starts == NULL) {
        mep_starts = new_hashmap(13, mep_start_delete_cb, NULL);
    }
    struct MepStart *mepstart = (struct MepStart *)hashmap_find(mep_starts, mep_name);
    if (mepstart) {
        if (mepstart->target != obj) {
            if (!obj) {
                log_error("Redefined MEP Start '%s' without target (original target is '%s')",
                          mep_name, pipelineobject_get_name(mepstart->target));
            } else if (!mepstart->target) {
                log_error("MEP Start '%s' with target '%s' conflict with previous definition without target",
                          mepstart->name, pipelineobject_get_name(obj));
            } else {
                log_error("Redefined MEP Start '%s' with mismatching target '%s' (original target is '%s')",
                        mepstart->name, pipelineobject_get_name(obj), pipelineobject_get_name(mepstart->target));
            }
            return false;
        }
        if (strcmp(stream_name, mepstart->stream_name) == 0) {
            // multiple instances of the same compound stream
            return true;
        }
        log_error("MEP Start '%s' defined twice, in streams '%s' and '%s'",
                mep_name, mepstart->stream_name, stream_name);
        return false;
    }
    mepstart = calloc_struct(MepStart);
    mepstart->name = strdup(mep_name);
    mepstart->stream_name = strdup(stream_name);
    mepstart->level = level;
    mepstart->pipe = pipe;
    if (obj) {
        mepstart->target = obj;
        pipelineobject_store_mep_start_name(obj, mep_name);
    }
    mepstart->pipe_pos_idx = idx;
    // for mepstart->pipe see oam_set_pipeline_for_mep_start()
    hashmap_insert(mep_starts, mepstart->name, mepstart);
    notification_register_source(mep_name, mep_start_notification_pull_fn, mepstart, 2000);
    return true;
}

struct MepStart *find_mep_start(const char *name)
{
    return (struct MepStart *)hashmap_find(mep_starts, name);
}

int foreach_mep_start(hashmap_cb *cb, void *userdata)
{
    return hashmap_foreach_sorted(mep_starts, cb, userdata);
}

int print_mep_start(const struct MepStart *start, FILE *cmd_w)
{
    return fprintf(cmd_w, "%s level %d in pipe %s at pos %d\n",
            start->name, start->level, start->pipe->name, start->pipe_pos_idx);
}

bool mep_start_in_stream(const struct MepStart *start, const char *stream)
{
    for (struct StreamNameAssociation *s=stream_names; s; s=s->next) {
        if (hashmap_contains(s->names, start->stream_name) && hashmap_contains(s->names, stream))
            return 1;
    }
    return 0;
}

// check when the last mask heartbeat received
// time < now+1sec: masked
// time > now+1sec: unmasked
static bool is_masked(const struct MepStart *mep, const struct timespec *now)
{
    // mask heartbeat timeout is 1 sec fixed
    struct timespec timeout = { .tv_sec = 1, .tv_nsec = 0 };
    struct timespec diff = { 0, 0 };
    timespecsub(now, &mep->last_mask_heartbeat, &diff);
    if (timespeccmp(&diff, &timeout, >)) {
        return false;
    } else {
        return true;
    }
}

static struct OamRequest *new_mask_request(const char *command, struct MepStart *start, int level)
{
    struct OamRequest *mask_req = parse_mask_command(command, NULL);
    request_set_level(mask_req, level);
    request_set_mepstart(mask_req, start);
    return mask_req;
}

static void *mask_checker_thread_fn(void *arg)
{
    struct MepStart *postmep = (struct MepStart *) arg;
    struct PipelineObject *target = postmep->target;
    struct timespec to_sleep = { .tv_sec = 1, .tv_nsec = 0 };
    bool is_regenerating = false;

    while (true) {
        int masked_mep_count = 0;
        int path_count = 0;
        struct timespec now;

        clock_gettime(CLOCK_REALTIME, &now);
        HASHMAP_ITERATE(target->meps, it) {
            const char *key = hash_iterator_key(&it);
            if (strstr(key, "_pre-")) {
                path_count += 1;
                if (is_masked(find_mep_start(key), &now)) {
                    masked_mep_count += 1;
                }
            }
        }

        log_debug("%s: masked/all paths: %d/%d", postmep->name, masked_mep_count, path_count);

        if (masked_mep_count > path_count) {
            log_error("more paths masked (%d) than available (%d)", masked_mep_count, path_count);
        } else if (masked_mep_count < path_count) {
            // Generate an unmask signal if some path unmasked
            if (is_regenerating == true) {
                struct OamRequest *mask_req = new_mask_request("unmask", postmep, target->auto_mip_level);
                initiate_request(mask_req);
                log_debug("%s: stop re-generate mask signal", postmep->name);
                is_regenerating = false;
                break;
            }
        } else if (masked_mep_count == path_count) {
            // Re-generate mask singal if all paths masked
            if (is_regenerating == false) {
                struct OamRequest *mask_req = new_mask_request("mask", postmep, target->auto_mip_level);
                initiate_request(mask_req);
                log_debug("%s: start re-generate mask signal", postmep->name);
                is_regenerating = true;
            }
        }

        int not_masked = path_count - masked_mep_count;
        seq_rec_set_latent_error_paths(target, not_masked);

        if (masked_mep_count == 0)
            break;

        clock_nanosleep(CLOCK_REALTIME, 0, &to_sleep, NULL);
    }
    postmep->mask_check_worker = NULL;
    return NULL;
}

// the only purpose of the worker right now is to
// check masked pre-MIPs and re-generate mask/unmask signal
void mep_start_wakeup_mask_checker(struct MepStart *start)
{
    if (start) {
        if (start->mask_check_worker == NULL) {
            start->mask_check_worker = thread_launch(mask_checker_thread_fn, start,
                                          "w/post-%s_%s", start->target->name, start->stream_name);
        } else {
            thread_wakeup(start->mask_check_worker);
        }
    }
}

static int copy_streamname(const char *key, void *value, void *userdata)
{
    struct HashMap *snames = (struct HashMap *)userdata;
    hashmap_insert(snames, strdup(key), value); // value is NULL
    return 1;
}

void oam_stream_names_in_pipeline(struct HashMap *names)
{
    for (struct StreamNameAssociation *s=stream_names; s; s=s->next) {
        HASHMAP_ITERATE(names, it) {
            const char *key = hash_iterator_key(&it);
            if (hashmap_contains(s->names, key)) {
                hashmap_foreach(names, copy_streamname, s->names);
                return;
            }
        }
    }

    // no existing stream set has a common subset with @names
    struct StreamNameAssociation *sa = calloc_struct(StreamNameAssociation);
    sa->names = new_hashmap(11, NULL, NULL);
    hashmap_foreach(names, copy_streamname, sa->names);
    sa->next = stream_names;
    stream_names = sa;
}

bool oam_start_background_ping(const char *name, const char *command)
{
    if (oam_initialized == false) {
        init_oam();
    }

    log_info("starting background ping command '%s'", name);

    if (strncmp(command, "ping", 4) != 0) {
        log_error("background command '%s' is not ping", name);
        return false;
    }
    struct OamRequest *ping_req = parse_ping_command(command+4, true, false, NULL);

    if (request_get_error(ping_req)) {
        log_error("background ping command '%s' invalid: %s", name, request_get_error(ping_req));
        delete_oam_request(ping_req);
        return false;
    }

    request_set_count(ping_req, 0);    // force infinite count

    struct StreamSessions *stream = get_stream_sessions(request_get_stream_name(ping_req));
    if (stream_live_session_count(stream) >= 14) {
        log_error("stream %s has too many sessions, can't start background ping", request_get_stream_name(ping_req));
        free(ping_req);
        return false;
    }
    return initiate_request(ping_req);
}

void mep_start_count_passed(struct MepStart *start, const struct Packet *pkt)
{
    __atomic_fetch_add(&start->packets_passed, 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&start->octets_passed, packet_length(pkt), __ATOMIC_RELAXED);
}

bool init_oam(void)
{
    if (oam_initialized) return true;

    init_msg_module(oam_default_iface != NULL);

    if (oam_default_iface || oam_cmd_iface) {
        log_info("Init OAM fuctionality:%s%s",
                oam_cmd_iface?" telnet interface":"",
                oam_default_iface?" reply interface":"");
    } else {
        oam_initialized = true;
        return true;
    }

    init_cmd_module();

    oam_initialized = true;
    return true;
}

void finish_oam(void)
{
    stop_all_sessions_of_connection(NULL); // stop the background sessions
    finish_cmd_module();
    finish_msg_module();
    delete_hashmap(mep_starts);
    delete_hashmap(oam_ifaces);

    for (struct StreamNameAssociation *s=stream_names; s;) {
        struct StreamNameAssociation *d = s;
        s = s->next;
        delete_hashmap(d->names);
        free(d);
    }

    log_info("Stopped OAM functionality");
}

// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#define OAM_INTERNAL

#include "oam.h"
#include "oam_command.h"
#include "oam_core.h"
#include "oam_message.h"
#include "oam_request.h"
#include "object.h"

#include "if_oam.h"
#include "hashmap.h"
#include "log.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>

DEFAULT_LOGGING_MODULE(OAM, INFO);


static struct HashMap *oam_ifaces;
static struct Interface *oam_default_iface = NULL;
static struct Interface *oam_cmd_iface = NULL;

static struct HashMap *mep_starts = NULL; // name -> struct MEPStart

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

static int mep_start_delete_cb(const char *key, void *value, void *userdata)
{
    (void)key;
    (void)userdata;
    struct MepStart *mepstart = (struct MepStart *)value;
    free(mepstart->name);
    free(mepstart->stream_name);
    free(mepstart);
    return 1;
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
    mepstart->target = obj;
    pipelineobject_store_mep_start_name(obj, mep_name);
    mepstart->pipe_pos_idx = idx;
    // for mepstart->pipe see oam_set_pipeline_for_mep_start()
    hashmap_insert(mep_starts, mepstart->name, mepstart);
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
    //TODO what is considered "same stream"?
    return strcmp(start->stream_name, stream) == 0;
}

struct OamEndPoint *oam_create_endpoint(const char *name, const char *stream, int level, bool stop)
{
    //TODO make sure that endpoints have unique names
    //      put them into the same hash as the startpoints?
    struct OamEndPoint *ret = calloc_struct(OamEndPoint);
    ret->name = strdup(name);
    ret->stream = strdup(stream);
    ret->level = level;
    ret->stop = stop;
    return ret;
}

struct OamEndPoint *oam_delete_endpoint(struct OamEndPoint *end)
{
    free(end->name);
    free(end->stream);
    free(end);
    return NULL;
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
    struct oam_request *ping_req = parse_ping_command(command+4, true, false, NULL);

    if (request_get_error(ping_req)) {
        log_error("background ping command '%s' invalid: %s", name, request_get_error(ping_req));
        delete_oam_request(ping_req);
        return false;
    }

    request_override_count(ping_req, 0);    // force infinite count

    struct StreamSessions *stream = get_stream_sessions(request_get_stream_name(ping_req));
    if (stream_live_session_count(stream) >= 14) {
        log_error("stream %s has too many sessions, can't start background ping", request_get_stream_name(ping_req));
        free(ping_req);
        return false;
    }
    return initiate_request(ping_req);
}


bool init_oam(void)
{
    if (oam_initialized) return true;

    bool have_command_iface = oam_cmd_iface != NULL;
    bool have_reply_iface = oam_default_iface != NULL;

    init_msg_module(have_command_iface, have_reply_iface);

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
    log_info("Stopped OAM functionality");
}

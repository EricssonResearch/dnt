// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#define OAM_INTERNAL

#include "oam_core.h"
#include "oam_message.h"
#include "oam_session.h"

#include "if_oam.h"
#include "if_oam_eth.h"
#include "hashmap.h"
#include "log.h"
#include "notification.h"
#include "oam.h"
#include "state.h"
#include "thread_utils.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>

DEFAULT_LOGGING_MODULE(OAM, INFO);


static struct HashMap *oam_ifaces;
static struct Interface *oam_default_ip_iface = NULL;
static struct Interface *oam_default_eth_iface = NULL;
static struct Interface *oam_cmd_iface = NULL;

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

    if(iface->type == IF_OAM) {
        if (oam_default_ip_iface == NULL) {
            oam_default_ip_iface = iface;
        } else {
            if (strcmp(iface->name, oam_default_ip_iface->name) < 0)
                oam_default_ip_iface = iface;
        }
    } else if(iface->type == IF_OAM_ETH) {
        if (oam_default_eth_iface == NULL) {
            oam_default_eth_iface = iface;
        } else {
            if (strcmp(iface->name, oam_default_eth_iface->name) < 0)
                oam_default_eth_iface = iface;
        }
    }
}

struct Interface *get_oam_interface(const char *ifname)
{
    return ifname[0] ? (struct Interface *)hashmap_find(oam_ifaces, ifname) : oam_default_ip_iface;
}

struct Interface *get_default_oam_ip_interface(void)
{
    return oam_default_ip_iface;
}

struct Interface *get_default_oam_eth_interface(void)
{
    return oam_default_eth_iface;
}

int foreach_oam_ifaces(hashmap_cb *cb, void *userdata)
{
    return hashmap_foreach_sorted(oam_ifaces, cb, userdata);
}

bool have_default_ip_iface(void)
{
    return oam_default_ip_iface != NULL;
}

unsigned short get_default_node_id(void)
{
    if(oam_default_ip_iface != NULL)
        return 0;
    if(oam_default_ip_iface->type == IF_OAM_ETH)
        return oam_eth_if_get_uid(oam_default_ip_iface);
    else
        return oamif_get_uid(oam_default_ip_iface);
}

// check when the last mask heartbeat received
// time < now+1sec: masked
// time > now+1sec: unmasked
/*static bool is_masked(const struct MepStart *mep, const struct timespec *now)
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
}*/

/*static void *mask_checker_thread_fn(void *arg)
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
}*/

// the only purpose of the worker right now is to
// check masked pre-MIPs and re-generate mask/unmask signal
/*void mep_start_wakeup_mask_checker(struct MepStart *start)
{
    if (start) {
        if (start->mask_check_worker == NULL) {
            start->mask_check_worker = thread_launch(mask_checker_thread_fn, start,
                                          "w/post-%s_%s", start->target->name, start->stream_name);
        } else {
            thread_wakeup(start->mask_check_worker);
        }
    }
}*/

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

    request_set_infinite_count(ping_req);

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

    init_session_module();
    init_message_module();

    if (oam_default_ip_iface || oam_default_eth_iface || oam_cmd_iface) {
        log_info("Init OAM fuctionality:%s%s%s",
                oam_cmd_iface?" telnet interface":"",
                oam_default_ip_iface?" reply interface":"",
                oam_default_eth_iface?" reply interface":"");
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
    finish_message_module();
    finish_session_module();
    delete_hashmap(oam_ifaces);

    log_info("Stopped OAM functionality");
}

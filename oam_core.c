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
    return ifname[0] ? (struct Interface *)hashmap_find(oam_ifaces, ifname) : (have_default_ip_iface()? oam_default_ip_iface : oam_default_eth_iface);
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

bool have_default_eth_iface(void)
{
    return oam_default_eth_iface != NULL;
}

unsigned short get_default_node_id(void)
{
    if(oam_default_ip_iface != NULL)
        return oamif_get_uid(oam_default_ip_iface);
    else if(oam_default_eth_iface != NULL)
        return oam_eth_if_get_uid(oam_default_eth_iface);
    else
        return 0;
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
    struct OamRequest *ping_req = parse_ping_command(command+4, true, false);

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
    return initiate_request(ping_req, NULL);
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

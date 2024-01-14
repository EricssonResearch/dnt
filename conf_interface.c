// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "conf_interface.h"
#include "conf_utils.h"
#include "inifile.h"
#include "interface.h"
#include "log.h"
#include "utils.h"

#include "if_eth.h"
#include "if_internal.h"
#include "if_ip.h"
#include "if_udp_in.h"
#include "if_udp_out.h"
#include "if_oam.h"
#include "if_oam_cmd.h"
#include "oam.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

DEFAULT_LOGGING_MODULE(CONFIG, WARNING)

struct ConfIfacesState {
    struct HashMap *ifaces;
};

struct TokenState {
    const char *ifname;
    char *type;
    char *iface;
    struct HashMap *params; // collect type-specific parameters
};

static bool iface_token_cb(char *token, void *userdata)
{
#define THROW(msg, ...)                                             \
    do {                                                            \
        log_error("interface %s error: " msg "\n",            \
                tstate->ifname, ##__VA_ARGS__);                     \
        return false;                                               \
    } while (0)

    struct TokenState *tstate = userdata;
    if (tstate->type) {
        char *key, *val;
        if (parse_assignment(token, &key, &val)) {
            if (strcmp(key, "iface") == 0) {
                if (tstate->iface == NULL) {
                    tstate->iface = strdup(val);
                } else {
                    THROW("iface parameter is duplicate %s %s", tstate->iface, val);
                }
            } else {
                // this may be a type-specific parameter, keep it
                if (hashmap_contains(tstate->params, key)) {
                    THROW("parameter %s is duplicate", key);
                }
                hashmap_insert(tstate->params, strdup(key), strdup(val));
            }
        } else {
            THROW("interface parameter '%s' is invalid", token);
        }
    } else {
        tstate->type = strdup(token);
    }

    return true;
#undef THROW
}

static int iface_cb(const char *key, void *value, void *userdata)
{
#define THROW(msg, ...)                                             \
    do {                                                            \
        log_error("interface %s error: " msg "\n",            \
                key, ##__VA_ARGS__);                                \
        free(tstate.type);                                          \
        delete_hashmap(tstate.params);                              \
        return 0;                                                   \
    } while (0)

    struct ConfIfacesState *state = userdata;
    char *desc = value;

    // skip the stream list
    if (strstr(key, ":streams"))
        return 1;

    struct TokenState tstate = {0};
    tstate.ifname = key;
    tstate.params = new_hashmap(7, NULL, NULL);
    if (!foreach_tokens(desc, iface_token_cb, &tstate)) {
        THROW("failed to parse parameters");
    }

    if (tstate.type == NULL) {
        THROW("type is unspecified");
    }

    if (strcmp(tstate.type, "eth") == 0) {
        if (tstate.iface == NULL) {
            THROW("hw interface is unspecified");
        }
        //TODO additional parameter: use 8 sockets or eBPF priority setting
        struct Interface *iface = new_eth_interface(key, tstate.iface);
        if (!iface) {
            THROW("failed to create ethernet interface");
        }
        hashmap_insert(state->ifaces, iface->name, iface);
    } else if (strcmp(tstate.type, "internal") == 0) {
        struct Interface *iface = new_internal_interface(key);
        if (!iface) {
            THROW("failed to create internal interface");
        }
        hashmap_insert(state->ifaces, iface->name, iface);
    } else if (strcmp(tstate.type, "ip") == 0) {
        struct Interface *iface = new_ip_interface(key, tstate.iface);
        if (!iface) {
            THROW("failed to create ip interface");
        }
        hashmap_insert(state->ifaces, iface->name, iface);
    } else if (strcmp(tstate.type, "udp-in") == 0) {
        unsigned port = 6635;
        unsigned ipver = 4;
        unsigned u;
        char err;
        char *port_str = hashmap_find(tstate.params, "port");
        if (port_str) {
            if (sscanf(port_str, "%i%c", &u, &err) != 1)
                THROW("port '%s' is invalid", port_str);
            if (u > 0xffff)
                THROW("port '%s' is invalid", port_str);
            port = u;
        }
        char *ipver_str = hashmap_find(tstate.params, "ipv");
        if (ipver_str) {
            if (sscanf(ipver_str, "%u%c", &u, &err) != 1)
                THROW("ip version '%s' is invalid", ipver_str);
            if (!(u == 4 || u == 6))
                THROW("ip version '%s' is invalid", ipver_str);
            ipver = u;
        }
        if (tstate.iface == NULL) {
            THROW("hw interface is unspecified");
        }
        struct Interface *iface = new_udp_in_interface(key, tstate.iface, port, ipver);
        if (!iface) {
            THROW("failed to create udp-in interface");
        }
        hashmap_insert(state->ifaces, iface->name, iface);
    } else if (strcmp(tstate.type, "udp-out") == 0) {
        unsigned srcport = 0;
        unsigned dstport = 6635;
        unsigned priority = 0;
        unsigned u;
        char err;
        char *port_str = hashmap_find(tstate.params, "srcport");
        if (port_str) {
            if (sscanf(port_str, "%i%c", &u, &err) != 1)
                THROW("srcport '%s' is invalid", port_str);
            if (u > 0xffff)
                THROW("srcport '%s' is invalid", port_str);
            srcport = u;
        }
        port_str = hashmap_find(tstate.params, "dstport");
        if (port_str) {
            if (sscanf(port_str, "%i%c", &u, &err) != 1)
                THROW("dstport '%s' is invalid", port_str);
            if (u > 0xffff)
                THROW("dstport '%s' is invalid", port_str);
            dstport = u;
        }
        char *dst_ip = hashmap_find(tstate.params, "dstip");
        if (dst_ip == NULL) {
            THROW("dstip is unspecified");
        }
        char *priority_str = hashmap_find(tstate.params, "prio");
        if (priority_str) {
            if (sscanf(priority_str, "%i%c", &u, &err) != 1)
                THROW("prio '%s' is invalid", priority_str);
            if (u > 7)
                THROW("prio '%s' is invalid", priority_str);
        }
        if (tstate.iface == NULL) {
            THROW("hw interface is unspecified");
        }
        struct Interface *iface = new_udp_out_interface(key, tstate.iface, srcport, dst_ip, dstport, priority);
        if (!iface) {
            THROW("failed to create udp-out interface");
        }
        hashmap_insert(state->ifaces, iface->name, iface);
    } else if (strcmp(tstate.type, "oam_cmd") == 0) {
        unsigned port = OAM_CMD_PORT;
        unsigned u;
        char err;
        char *oam_cmd_ip = hashmap_find(tstate.params, "ip");
        char *port_str = hashmap_find(tstate.params, "port");
        if (port_str) {
            if (sscanf(port_str, "%i%c", &u, &err) != 1)
                THROW("oam_cmd_port '%s' is invalid", port_str);
            if (u > 0xffff)
                THROW("oam_cmd_port '%s' is invalid", port_str);
            port = u;
        }
        struct Interface *iface = new_oam_cmd_interface(key, tstate.iface, oam_cmd_ip, port);
        if (!iface) {
            THROW("failed to create oam_cmd interface");
        }
        hashmap_insert(state->ifaces, iface->name, iface);
    } else if (strcmp(tstate.type, "oam") == 0) {
        unsigned oam_port = OAM_PORT;
        unsigned u;
        char err;
        char *oam_ip = hashmap_find(tstate.params, "ip");
        if (oam_ip == NULL) {
            THROW("oam_ip is unspecified.");
        }
        char *port_str = hashmap_find(tstate.params, "port");
        if (port_str) {
            if (sscanf(port_str, "%i%c", &u, &err) != 1)
                THROW("oam_port '%s' is invalid", port_str);
            if (u > 0xffff)
                THROW("oam_port '%s' is invalid", port_str);
            oam_port = u;
        }
        struct Interface *iface = new_oam_interface(key, oam_ip, oam_port);
        if (!iface) {
            THROW("failed to create oam interface");
        }
        hashmap_insert(state->ifaces, iface->name, iface);
    } else {
        THROW("unknown interface type '%s'", tstate.type);
    }

    free(tstate.type);
    free(tstate.iface);
    delete_hashmap(tstate.params);
    return 1;
#undef THROW
}

static int iface_delete_cb(const char *key, void *value, void *userdata)
{
    (void)key; // owned by the interface
    (void)userdata;
    struct Interface *iface = value;
    close_iface(iface);
    return 1;
}

struct HashMap *parse_interfaces(const struct IniSection *interfaces_section)
{
    struct ConfIfacesState state = {0};
    state.ifaces = new_hashmap(11, iface_delete_cb, NULL);

    if (!hashmap_foreach(interfaces_section->contents, iface_cb, &state)) {
        log_error("an interface is invalid\n");
        delete_hashmap(state.ifaces);
        return NULL;
    }

    return state.ifaces;
}


struct ConfIfaceStreamsState {
    const struct HashMap *ifaces;
    const struct HashMap *streams;
    struct HashMap *iface_streams;
};

struct ConfIfaceTokenState {
    const struct HashMap *streams;
    struct ConfStreamList *iface_stream_list;
};

static bool iface_stream_token_cb(char *token, void *userdata)
{
    struct ConfIfaceTokenState *tstate = userdata;

    struct ConfStream *s = hashmap_find(tstate->streams, token);
    if (s != NULL) {
        struct ConfStreamList *sl = calloc_struct(ConfStreamList);
        sl->stream = s;
        sl->stream_name = strdup(token);
        sl->next = tstate->iface_stream_list;
        tstate->iface_stream_list = sl;
        return true;
    } else {
        log_error("no stream named '%s'\n", token);
        return false;
    }
}

static int iface_stream_cb(const char *key, void *value, void *userdata)
{
    struct ConfIfaceStreamsState *state = userdata;
    char *str = value;

    char *is = strstr(key, ":streams");
    // skip the lines that are not stream list
    if (is == NULL)
        return 1;

    // verify that there is no garbage after :streams
    if (is-key != (long int)strlen(key) - 8) {
        log_error("interface streams '%s' invalid\n", key);
        return 0;
    }

    // find the interface
    char *ifname = strndup(key, is-key);
    struct Interface *iface = hashmap_find(state->ifaces, ifname);
    if (iface == NULL) {
        log_error("parsing streams for interfaces: unknown interface '%s'\n", ifname);
        free(ifname);
        return 0;
    }

    struct ConfIfaceTokenState tokstate = {
        .streams = state->streams,
        .iface_stream_list = NULL,
    };
    if (!foreach_tokens(str, iface_stream_token_cb, &tokstate)) {
        log_error("invalid streams for interface '%s'\n", ifname);
        free(ifname);
        return 0;
    }

    REVERSE_LIST(tokstate.iface_stream_list);
    hashmap_insert(state->iface_streams, ifname, tokstate.iface_stream_list);
    log_info("interface %s receives streams:\n", ifname);
    for (struct ConfStreamList *i=tokstate.iface_stream_list; i; i=i->next) {
        log_info("  %s\n", i->stream_name);
    }
    return 1;
}

static int iface_stream_delete_cb(const char *key, void *value, void *userdata)
{
    (void)userdata;
    free((char*)key);
    struct ConfStreamList *list = value;
    while (list) {
        struct ConfStreamList *del = list;
        list = list->next;
        free(del->stream_name);
        free(del);
    }
    return 1;
}

struct HashMap *parse_interface_streams(const struct IniSection *interfaces_section,
        const struct HashMap *ifaces, const struct HashMap *streams)
{
    struct ConfIfaceStreamsState state = {
        .ifaces = ifaces,
        .streams = streams,
        .iface_streams = new_hashmap(13, iface_stream_delete_cb, NULL),
    };

    if (!hashmap_foreach(interfaces_section->contents, iface_stream_cb, &state)) {
        log_error("an interface streams line is invalid\n");
        delete_hashmap(state.iface_streams);
        return NULL;
    }

    return state.iface_streams;
}

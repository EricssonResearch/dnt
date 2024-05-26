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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

DEFAULT_LOGGING_MODULE(CONFIG, WARNING);

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
        log_error("interface %s error: " msg,                       \
                tstate->ifname, ##__VA_ARGS__);                     \
        return false;                                               \
    } while (0)

    struct TokenState *tstate = (struct TokenState *)userdata;
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
        log_error("interface %s error: " msg,                       \
                key, ##__VA_ARGS__);                                \
        free(tstate.type);                                          \
        delete_hashmap(tstate.params);                              \
        return 0;                                                   \
    } while (0)

    struct ConfIfacesState *state = (struct ConfIfacesState *)userdata;
    char *desc = (char *)value;

    // skip the stream list
    if (strstr(key, ":streams"))
        return 1;

    struct TokenState tstate;
    memset(&tstate, 0, sizeof(tstate));
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
        char *port_str = (char *)hashmap_find(tstate.params, "port");
        if (port_str) {
            if (sscanf(port_str, "%i%c", &u, &err) != 1)
                THROW("port '%s' is invalid", port_str);
            if (u > 0xffff)
                THROW("port '%s' is invalid", port_str);
            port = u;
        }
        char *ipver_str = (char *)hashmap_find(tstate.params, "ipv");
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
        char *port_str = (char *)hashmap_find(tstate.params, "srcport");
        if (port_str) {
            if (sscanf(port_str, "%i%c", &u, &err) != 1)
                THROW("srcport '%s' is invalid", port_str);
            if (u > 0xffff)
                THROW("srcport '%s' is invalid", port_str);
            srcport = u;
        }
        port_str = (char *)hashmap_find(tstate.params, "dstport");
        if (port_str) {
            if (sscanf(port_str, "%i%c", &u, &err) != 1)
                THROW("dstport '%s' is invalid", port_str);
            if (u > 0xffff)
                THROW("dstport '%s' is invalid", port_str);
            dstport = u;
        }
        char *dst_ip = (char *)hashmap_find(tstate.params, "dstip");
        if (dst_ip == NULL) {
            THROW("dstip is unspecified");
        }
        char *priority_str = (char *)hashmap_find(tstate.params, "prio");
        if (priority_str) {
            if (sscanf(priority_str, "%i%c", &priority, &err) != 1)
                THROW("prio '%s' is invalid", priority_str);
            if (priority > 7)
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
        char *oam_cmd_ip = (char *)hashmap_find(tstate.params, "ip");
        char *port_str = (char *)hashmap_find(tstate.params, "port");
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
        char *oam_ip = (char *)hashmap_find(tstate.params, "ip");
        if (oam_ip == NULL) {
            THROW("oam_ip is unspecified.");
        }
        char *port_str = (char *)hashmap_find(tstate.params, "port");
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

bool parse_interfaces(struct HashMap *ifaces, const struct IniSection *interfaces_section)
{
    struct ConfIfacesState state = {0};
    state.ifaces = ifaces;

    if (!hashmap_foreach(interfaces_section->contents, iface_cb, &state)) {
        log_error("an interface is invalid");
        return false;
    }

    return true;
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
    struct ConfIfaceTokenState *tstate = (struct ConfIfaceTokenState *)userdata;

    struct ConfStream *s = (struct ConfStream *)hashmap_find(tstate->streams, token);
    if (s != NULL) {
        struct ConfStreamList *sl = calloc_struct(ConfStreamList);
        sl->stream = s;
        sl->stream_name = strdup(token);
        sl->next = tstate->iface_stream_list;
        tstate->iface_stream_list = sl;
        return true;
    } else {
        log_error("no stream named '%s'", token);
        return false;
    }
}

static int iface_stream_cb(const char *key, void *value, void *userdata)
{
    struct ConfIfaceStreamsState *state = (struct ConfIfaceStreamsState *)userdata;
    char *str = (char *)value;

    const char *is = strstr(key, ":streams");
    // skip the lines that are not stream list
    if (is == NULL)
        return 1;

    // verify that there is no garbage after :streams
    if (is-key != (long int)strlen(key) - 8) {
        log_error("interface streams '%s' invalid", key);
        return 0;
    }

    // find the interface
    char *ifname = strndup(key, is-key);
    struct Interface *iface = (struct Interface *)hashmap_find(state->ifaces, ifname);
    if (iface == NULL) {
        log_error("parsing streams for interfaces: unknown interface '%s'", ifname);
        free(ifname);
        return 0;
    }

    struct ConfIfaceTokenState tokstate = {
        .streams = state->streams,
        .iface_stream_list = NULL,
    };
    if (!foreach_tokens(str, iface_stream_token_cb, &tokstate)) {
        log_error("invalid streams for interface '%s'", ifname);
        free(ifname);
        return 0;
    }

    // check for duplicates in the list
    if (tokstate.iface_stream_list) {
        for (struct ConfStreamList *i=tokstate.iface_stream_list; i->next; i=i->next) {
            for (struct ConfStreamList *j=i->next; j; j=j->next) {
                if (strcmp(i->stream_name, j->stream_name) == 0) {
                    log_error("interface '%s' receives stream '%s' twice", ifname, i->stream_name);
                    free(ifname);
                    return 0;
                }
            }
        }
    }

    REVERSE_LIST(tokstate.iface_stream_list);
    hashmap_insert(state->iface_streams, ifname, tokstate.iface_stream_list);
    log_info("interface %s receives streams:", ifname);
    for (struct ConfStreamList *i=tokstate.iface_stream_list; i; i=i->next) {
        log_info("  %s", i->stream_name);
    }
    return 1;
}

bool parse_interface_streams(struct HashMap *iface_streams, const struct IniSection *interfaces_section,
        const struct HashMap *ifaces, const struct HashMap *streams)
{
    struct ConfIfaceStreamsState state = {
        .ifaces = ifaces,
        .streams = streams,
        .iface_streams = iface_streams,
    };

    if (!hashmap_foreach(interfaces_section->contents, iface_stream_cb, &state)) {
        log_error("an interface streams line is invalid");
        return false;
    }

    return true;
}

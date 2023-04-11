
#include "conf_interface.h"
#include "conf_utils.h"
#include "inifile.h"
#include "interface.h"
#include "utils.h"

#include "if_eth.h"
#include "if_internal.h"
#include "if_udp_in.h"
#include "if_udp_out.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct ConfIfacesState {
    struct Interface *ifaces;
    unsigned iface_count;
    unsigned i;
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
        fprintf(stderr, "interface %s error: " msg "\n",            \
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
        fprintf(stderr, "interface %s error: " msg "\n",            \
                key, ##__VA_ARGS__);                                \
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
    if (tstate.iface == NULL) {
        THROW("hw interface is unspecified");
    }

    if (strcmp(tstate.type, "eth") == 0) {
        //TODO additional parameter: use 8 sockets or eBPF priority setting
        if (!init_eth_interface(state->ifaces+state->i, key, tstate.iface)) {
            THROW("failed to create ethernet interface");
        }
    } else if (strcmp(tstate.type, "internal") == 0) {
        if (!init_internal_interface(state->ifaces+state->i, key)) {
            THROW("failed to create internal interface");
        }
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
        char *ipver_str = hashmap_find(tstate.params, "ip");
        if (ipver_str) {
            if (sscanf(ipver_str, "%u%c", &u, &err) != 1)
                THROW("ip version '%s' is invalid", ipver_str);
            if (!(u == 4 || u == 6))
                THROW("ip version '%s' is invalid", ipver_str);
            ipver = u;
        }
        if (!init_udp_in_interface(state->ifaces+state->i, key, tstate.iface, port, ipver)) {
            THROW("failed to create udp-in interface");
        }
    } else if (strcmp(tstate.type, "udp-out") == 0) {
        unsigned port = 6635;
        unsigned priority = 0;
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
        if (!init_udp_out_interface(state->ifaces+state->i, key, tstate.iface, port, dst_ip, priority)) {
            THROW("failed to create udp-out interface");
        }
    } else {
        THROW("cannot yet create type '%s'", tstate.type);
    }
    state->i++;

    free(tstate.type);
    free(tstate.iface);
    delete_hashmap(tstate.params);
    return 1;
#undef THROW
}

struct Interface *parse_interfaces(struct IniSection *interfaces_section, unsigned *iface_count)
{
    struct ConfIfacesState state = {0};
    state.iface_count = hashmap_count(interfaces_section->contents);
    state.ifaces = calloc_struct_array(Interface, state.iface_count); //TODO this overallocates

    if (!hashmap_foreach(interfaces_section->contents, iface_cb, &state)) {
        fprintf(stderr, "an interface is invalid\n");
        for (unsigned i=0; i<state.iface_count; i++) {
            iface_unref(&state.ifaces[i]);
        }
        free(state.ifaces);
        return NULL;
    }

    //TODO realloc state.ifaces (or store them in a hash table)
    //      note: here we can realloc but later streams will have direct pointers into this array
    *iface_count = state.i;
    return state.ifaces;
}


struct ConfIfaceStreamsState {
    struct Interface *ifaces;
    unsigned iface_count;
    struct HashMap *streams;
    struct HashMap *iface_streams;
};

struct ConfIfaceTokenState {
    struct HashMap *streams;
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
        fprintf(stderr, "no stream named '%s'\n", token);
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

    // find the interface
    char *ifname = strndup(key, is-key);
    struct Interface *iface = NULL;
    for (unsigned i=0; i<state->iface_count; i++) {
        if (strcmp(state->ifaces[i].name, ifname) == 0) {
            iface = &state->ifaces[i];
            break;
        }
    }
    if (iface == NULL) {
        fprintf(stderr, "parsing streams for interfaces: unknown interface '%s'\n", ifname);
        free(ifname);
        return 0;
    }

    struct ConfIfaceTokenState tokstate = {
        .streams = state->streams,
        .iface_stream_list = NULL,
    };
    if (!foreach_tokens(str, iface_stream_token_cb, &tokstate)) {
        fprintf(stderr, "invalid streams for interface '%s'\n", ifname);
        free(ifname);
        return 0;
    }

    REVERSE_LIST(tokstate.iface_stream_list);
    hashmap_insert(state->iface_streams, ifname, tokstate.iface_stream_list);
    printf("interface %s receives streams:\n", ifname);
    for (struct ConfStreamList *i=tokstate.iface_stream_list; i; i=i->next) {
        printf("  %s\n", i->stream_name);
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

struct HashMap *parse_interface_streams(struct IniSection *interfaces_section,
        struct Interface *ifaces, unsigned iface_count, struct HashMap *streams)
{
    struct ConfIfaceStreamsState state = {
        .ifaces = ifaces,
        .iface_count = iface_count,
        .streams = streams,
        .iface_streams = new_hashmap(13, iface_stream_delete_cb, NULL),
    };

    if (!hashmap_foreach(interfaces_section->contents, iface_stream_cb, &state)) {
        fprintf(stderr, "an interface streams line is invalid\n");
        delete_hashmap(state.iface_streams);
    }

    return state.iface_streams;
}

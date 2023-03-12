
#include "conf_interface.h"
#include "conf_utils.h"
#include "inifile.h"
#include "interface.h"
#include "utils.h"

#include "if_eth.h"

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
                if (tstate->params == NULL) {
                    tstate->params = new_hashmap(7, NULL, NULL);
                }
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
        if (!init_eth_interface(state->ifaces+state->i, key, tstate.iface)) {
            THROW("failed to create ethernet interface");
        }
        state->i++;
    } else {
        THROW("cannot yet create type '%s'", tstate.type);
    }

    free(tstate.type);
    free(tstate.iface);
    if (tstate.params)
        delete_hashmap(tstate.params);
    return 1;
#undef THROW
}

struct Interface *parse_interfaces(struct IniSection *interfaces_section, unsigned *iface_count)
{
    //TODO read the streams line for the interface
    //      that should be a separate function: process_interface_streams()

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

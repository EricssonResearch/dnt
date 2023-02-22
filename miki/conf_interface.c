
#include "conf_interface.h"
#include "conf_utils.h"
#include "inifile.h"
#include "interface.h"
#include "utils.h"

#include "if_eth.h"

#include <stdlib.h>
#include <string.h>

struct ConfIfacesState {
    struct Interface *ifaces;
    unsigned iface_count;
    unsigned i;
};

struct TokenState {
    char *type;
    char *iface;
    struct HashMap *params; // collect type-specific parameters
};

static bool token_cb(char *str, void *userdata)
{
    struct TokenState *tstate = userdata;
    char *key, *val;
    if (parse_assignment(str, &key, &val)) {
        if (strcmp(key, "iface") == 0) {
            if (tstate->iface == NULL) {
                tstate->iface = strdup(val);
            } else {
                //TODO throw exception: iface already specified
            }
        } else if (strcmp(key, "type") == 0) {
            if (tstate->type == NULL) {
                tstate->type = strdup(val);
            } else {
                //TODO throw exception: type already specified
            }
        } else {
            // this may be a type-specific parameter, keep it
            if (tstate->params == NULL) {
                tstate->params = new_hashmap(7, NULL, NULL);
            }
            if (hashmap_contains(tstate->params, key)) {
                //TODO throw exception: parameter is duplicate
            }
            hashmap_insert(tstate->params, strdup(key), strdup(val));
        }
    } else {
        //TODO throw exception: interface parameter is invalid
    }
    return true;
}

static void iface_cb(const char *key, void *value, void *userdata)
{
    struct ConfIfacesState *state = userdata;
    char *desc = value;

    struct TokenState tstate = {0};
    foreach_tokens(desc, token_cb, userdata);

    if (tstate.type == NULL) {
        //TODO throw exception: interface type is unspecified
        return;
    }

    if (strcmp(tstate.type, "eth") == 0) {
        if (tstate.iface == NULL) {
            //TODO throw exception: interface is unspecified
        }
        //TODO additional parameter: use 8 sockets or eBPF priority setting
        if (!init_eth_interface(state->ifaces+state->i, key, tstate.iface)) {
            //TODO throw exception: failed to create ethernet interface
        }
        state->i++;
    } else {
        //TODO throw exception: we don't yet know how to create this interface type
    }

    //TODO cleanup tstate
}


struct Interface *process_interfaces(struct IniSection *interfaces_section, unsigned *iface_count)
{
    struct ConfIfacesState state = {0};
    state.iface_count = hashmap_count(interfaces_section->contents);
    state.ifaces = calloc_struct_array(Interface, state.iface_count);

    hashmap_foreach(interfaces_section->contents, iface_cb, &state);

    *iface_count = state.iface_count;
    return state.ifaces;
}

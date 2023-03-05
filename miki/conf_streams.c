
#include "conf_streams.h"
#include "conf_actions.h"
#include "conf_packet.h"
#include "conf_utils.h"
#include "inifile.h"
#include "interface.h"
#include "parsetree.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ConfStreamState {
    struct HashMap *streams;
    struct IniSection *streams_section;
    struct Interface *ifaces;
    unsigned ifcount;
    struct HashMap *objects;
};


static void packetline_cb(const char *key, void *value, void *userdata)
{
    struct ConfStreamState *state = userdata;
    char *packetline = value;

    const char *colon = strchr(key, ':');
    if (colon) {
        if (strcmp(colon+1, "packet") == 0) {
            char *streamname = strndup(key, colon-key);

            struct HeaderDescriptor *headers = process_packet_line(streamname, packetline);
            if (headers == NULL) {
                //TODO error
            }

            // find the matching :actions line
            int achars = snprintf(NULL, 0, "%s:actions", streamname);
            char *actionname = malloc((achars+1)*sizeof(char));
            snprintf(actionname, achars+1, "%s:actions", streamname);
            char *actionline = inisection_get(state->streams_section, actionname);
            if (actionline == NULL) {
                //TODO error
            }

            struct ConfAction *actions = process_actions_line(streamname, actionline,
                    headers, state->ifaces, state->ifcount,
                    state->objects, state->streams_section);
            if (actions == NULL) {
                //TODO error
            }
            printf("Stream %s actions:\n", streamname);
            print_actions(actions);

            // find the matching :iface line (it's optional)
            int ichars = snprintf(NULL, 0, "%s:iface", streamname);
            char *ifacename = malloc((ichars+1)*sizeof(char));
            snprintf(ifacename, ichars+1, "%s:iface", streamname);
            char *ifaceline = inisection_get(state->streams_section, ifacename);
            struct Interface *recv_iface = NULL;
            if (ifaceline) {
                for (unsigned i=0; i<state->ifcount; i++) {
                    if (strcmp(state->ifaces[i].name, ifaceline) == 0) {
                        recv_iface = state->ifaces + i;
                    }
                }
                if (recv_iface == NULL) {
                    //TODO error
                }
            }

            struct ConfStream *stream = calloc_struct(ConfStream);
            stream->actions = actions;
            stream->packet = headers;
            stream->recv_iface = recv_iface;
            hashmap_insert(state->streams, strdup(streamname), stream);
        }
    }
}

static void checkline_cb(const char *key, void *value, void *userdata)
{
    struct ConfStreamState *state = userdata;
    (void)value;

    const char *colon = strchr(key, ':');
    if (colon) {
        char *streamname = strndup(key, colon-key);
        if (strcmp(colon+1, "actions") == 0) {
            if (!hashmap_contains(state->streams, streamname)) {
                //TODO error
            }
        } else if (strcmp(colon+1, "iface") == 0) {
            if (!hashmap_contains(state->streams, streamname)) {
                //TODO error
            }
        }
        free(streamname);
    }
}

struct HashMap *parse_streams(struct IniSection *streams_section,
        struct Interface *ifaces, unsigned ifcount,
        struct HashMap *objects)
{
    struct ConfStreamState state = {
        .streams_section = streams_section,
        .ifaces = ifaces,
        .ifcount = ifcount,
        .objects = objects,
        .streams = new_hashmap(29, NULL, NULL) //TODO we need a delete callback
    };

    hashmap_foreach(streams_section->contents, packetline_cb, &state);

    // search for :actions and :iface lines that have no matching :packet line
    hashmap_foreach(streams_section->contents, checkline_cb, &state);

    return state.streams;
}


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


static int packetline_cb(const char *key, void *value, void *userdata)
{
#define THROW(msg, ...)                                             \
    do {                                                            \
        fprintf(stderr, "stream '%s' error: " msg "\n",             \
                 streamname, ##__VA_ARGS__);                        \
        free(streamname);                                           \
        free(matchname);                                            \
        free(actionname);                                           \
        delete_header_list(headers);                                \
        delete_confaction_list(actions);                            \
        return 0;                                                   \
    } while (0)

    struct ConfStreamState *state = userdata;
    char *packetline = value;

    const char *colon = strchr(key, ':');
    if (colon) {
        if (strcmp(colon+1, "packet") == 0) {
            char *streamname = strndup(key, colon-key);
            char *matchname  = NULL;
            char *actionname = NULL;
            struct HeaderDescriptor *headers = NULL;
            struct ConfAction *actions = NULL;

            printf("parsing stream '%s'\n", streamname);

            headers = parse_packet_line(streamname, packetline);
            if (headers == NULL) {
                THROW("packet line invalid");
            }
            //TODO print_packets()

            // find the corresponding :match line
            int chars = snprintf(NULL, 0, "%s:match", streamname);
            matchname = malloc((chars+1)*sizeof(char));
            snprintf(matchname, chars+1, "%s:match", streamname);
            char *matchline = inisection_get(state->streams_section, matchname);
            if (matchline == NULL) {
                THROW("match line not found");
            }
            if (!parse_match_line(streamname, headers, matchline) > 0) {
                THROW("match line invalid");
            }
            //TODO print_matches()

            // find the corresponding :actions line
            chars = snprintf(NULL, 0, "%s:actions", streamname);
            actionname = malloc((chars+1)*sizeof(char));
            snprintf(actionname, chars+1, "%s:actions", streamname);
            char *actionline = inisection_get(state->streams_section, actionname);
            if (actionline == NULL) {
                THROW("actions line not found");
            }
            actions = parse_actions_line(streamname, actionline,
                    headers, state->ifaces, state->ifcount,
                    state->objects, state->streams_section);
            if (actions == NULL) {
                THROW("actions invalid");
            }
            printf("Stream %s actions:\n", streamname);
            confactions_print(actions);


            struct ConfStream *stream = calloc_struct(ConfStream);
            stream->actions = actions;
            stream->packet = headers;
            hashmap_insert(state->streams, strdup(streamname), stream);
            free(streamname);
            free(matchname);
            free(actionname);
        }
    }
    return 1;
}

static int checkline_cb(const char *key, void *value, void *userdata)
{
    struct ConfStreamState *state = userdata;
    (void)value;

    const char *colon = strchr(key, ':');
    if (colon) {
        char *streamname = strndup(key, colon-key);
        if (strcmp(colon+1, "actions") == 0) {
            /* TODO we agreed that we don't need this restriction
            if (!hashmap_contains(state->streams, streamname)) {
                fprintf(stderr, "stream '%s' has actions line but no packet line\n", streamname);
                return 0;
            }*/
        } else if (strcmp(colon+1, "match") == 0) {
            if (!hashmap_contains(state->streams, streamname)) {
                fprintf(stderr, "stream '%s' has match line but no packet line\n", streamname);
                return 0;
            }
        }
        free(streamname);
    }
    return 1;
}

static int delstream_cb(const char *key, void *value, void *userdata)
{
    (void)userdata;
    struct ConfStream *stream = value;
    delete_confstream(stream);
    free((char*)key);
    return 1;
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
        .streams = new_hashmap(29, delstream_cb, NULL),
    };

    if (!hashmap_foreach(streams_section->contents, packetline_cb, &state)) {
        fprintf(stderr, "failed to parse the streams\n");
        delete_hashmap(state.streams);
        return NULL;
    }

    // search for :actions and :match lines that have no corresponding :packet line
    if (!hashmap_foreach(streams_section->contents, checkline_cb, &state)) {
        fprintf(stderr, "failed to parse the streams\n");
        delete_hashmap(state.streams);
        return NULL;
    }

    return state.streams;
}

struct ConfStream *delete_confstream(struct ConfStream *stream)
{
    if (!stream) return NULL;

    delete_header_list(stream->packet);
    delete_confaction_list(stream->actions);
    free(stream);

    return NULL;
}


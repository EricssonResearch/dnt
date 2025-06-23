// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "conf_streams.h"
#include "conf_actions.h"
#include "conf_packet.h"
#include "log.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

DEFAULT_LOGGING_MODULE(CONFIG, WARNING);

struct ConfStreamState {
    struct HashMap *streams;
    const struct IniSection *streams_section;
    const struct HashMap *ifaces;
    const struct HashMap *objects;
};


static int packetline_cb(const char *key, void *value, void *userdata)
{
#define THROW(msg, ...)                                             \
    do {                                                            \
        log_error("stream '%s' error: " msg,                        \
                 streamname, ##__VA_ARGS__);                        \
        free(streamname);                                           \
        free(matchname);                                            \
        free(actionname);                                           \
        delete_header_list(headers);                                \
        delete_confaction_list(actions);                            \
        return 0;                                                   \
    } while (0)

    struct ConfStreamState *state = (struct ConfStreamState *)userdata;
    char *packetline = (char *)value;

    const char *colon = strchr(key, ':');
    if (colon) {
        if (strcmp(colon+1, "packet") == 0) {
            char *streamname = strndup(key, colon-key);
            char *matchname  = NULL;
            char *actionname = NULL;
            struct HeaderDescriptor *headers = NULL;
            struct ConfAction *actions = NULL;

            log_info("Parsing stream '%s'", streamname);

            headers = parse_packet_line(streamname, packetline);
            if (headers == NULL) {
                THROW("packet line invalid");
            }

            // find the corresponding :match line
            int chars = snprintf(NULL, 0, "%s:match", streamname);
            matchname = (char *)malloc((chars+1)*sizeof(char));
            snprintf(matchname, chars+1, "%s:match", streamname);
            char *matchline = inisection_get(state->streams_section, matchname);
            if (matchline == NULL) {
                THROW("match line not found");
            }
            if (!parse_match_line(streamname, headers, matchline) > 0) {
                THROW("match line invalid");
            }

            log_info("Stream %s headers:", streamname);
            confheaders_print(headers);

            // find the corresponding :actions line
            chars = snprintf(NULL, 0, "%s:actions", streamname);
            actionname = (char *)malloc((chars+1)*sizeof(char));
            snprintf(actionname, chars+1, "%s:actions", streamname);
            char *actionline = inisection_get(state->streams_section, actionname);
            if (actionline == NULL) {
                THROW("actions line not found");
            }
            actions = parse_actions_line(streamname, actionline,
                    headers, state->ifaces,
                    state->objects, state->streams_section);
            if (actions == NULL) {
                THROW("actions invalid");
            }

            log_info("Stream %s actions:", streamname);
            confactions_log(actions, 2);


            struct ConfStream *stream = calloc_struct(ConfStream);
            stream->actions = actions;
            stream->headers = headers;
            hashmap_insert(state->streams, strdup(streamname), stream);
            free(streamname);
            free(matchname);
            free(actionname);
        }
    } else if (strcmp(key, "notification_session") == 0) {
        // this is a special stream for the notification framework
        struct HeaderDescriptor *headers = NULL;
        struct ConfAction *actions = NULL;
        char *streamname = strdup(key);
        char *matchname = NULL;
        char *actionname = NULL;

        log_info("Parsing stream '%s'", streamname);

        headers = calloc_struct(HeaderDescriptor);
        headers->name = strdup("payload");
        headers->id = PROTO_ID_PAYLOAD;

        actions = parse_actions_line(streamname, packetline,
                headers, state->ifaces, state->objects, state->streams_section);
        if (actions == NULL) {
            THROW("notification_session actions invalid");
        }

        struct ConfStream *stream = calloc_struct(ConfStream);
        stream->actions = actions;
        stream->headers = headers;
        hashmap_insert(state->streams, strdup("notification_session"), stream);
        free(streamname);
    }
    return 1;
}

static int checkline_cb(const char *key, void *value, void *userdata)
{
    struct ConfStreamState *state = (struct ConfStreamState *)userdata;
    (void)value;

    const char *colon = strchr(key, ':');
    if (colon) {
        char *streamname = strndup(key, colon-key);
        if (strcmp(colon+1, "actions") == 0) {
            /* TODO we agreed that we don't need this restriction
            if (!hashmap_contains(state->streams, streamname)) {
                log_error("stream '%s' has actions line but no packet line", streamname);
                return 0;
            }*/
        } else if (strcmp(colon+1, "match") == 0) {
            if (!hashmap_contains(state->streams, streamname)) {
                log_error("stream '%s' has match line but no packet line", streamname);
                free(streamname);
                return 0;
            }
        }
        free(streamname);
    }
    return 1;
}

bool parse_streams(struct HashMap *streams, const struct IniSection *streams_section,
        const struct HashMap *ifaces, const struct HashMap *objects)
{
    struct ConfStreamState state = {
        .streams = streams,
        .streams_section = streams_section,
        .ifaces = ifaces,
        .objects = objects,
    };

    if (!hashmap_foreach(streams_section->contents, packetline_cb, &state)) {
        log_error("failed to parse the streams");
        return false;
    }

    // search for :actions and :match lines that have no corresponding :packet line
    if (!hashmap_foreach(streams_section->contents, checkline_cb, &state)) {
        log_error("failed to parse the streams");
        return false;
    }

    return true;
}

struct ConfStream *delete_confstream(struct ConfStream *stream)
{
    if (!stream) return NULL;

    delete_header_list(stream->headers);
    delete_confaction_list(stream->actions);
    free(stream);

    return NULL;
}


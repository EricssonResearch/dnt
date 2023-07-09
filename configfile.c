// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "configfile.h"
#include "conf_actions.h"
#include "conf_interface.h"
#include "conf_object.h"
#include "conf_streams.h"
#include "action.h"
#include "inifile.h"
#include "interface.h"
#include "parsetree.h"
#include "pipeline.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int remove_comment(const char *key, void *value, void *userdata)
{
    (void)key;
    (void)userdata;
    char *v = value;
    char *cstart;
    cstart = strchr(v, ';');
    if (cstart) *cstart = 0;
    cstart = strchr(v, '#');
    if (cstart) *cstart = 0;
    return 1;
}

static void remove_comments_from_values(struct IniSection *ini)
{
    struct IniSection *i = ini;
    while (i) {
        hashmap_foreach(i->contents, remove_comment, NULL);
        i = i->next;
    }
}

static const char *find_duplicate_sections(struct IniSection *ini)
{
    bool ifs_found = false;
    bool obj_found = false;
    bool str_found = false;

    struct IniSection *i = ini;
    while (i) {
        if (strcmp(i->name, "interfaces") == 0) {
            if (ifs_found)
                return "interfaces";
            else
                ifs_found = true;
        }
        if (strcmp(i->name, "objects") == 0) {
            if (obj_found)
                return "objects";
            else
                obj_found = true;
        }
        if (strcmp(i->name, "streams") == 0) {
            if (str_found)
                return "streams";
            else
                str_found = true;
        }
        i = i->next;
    }

    return NULL;
}

static const char *find_unknown_section(struct IniSection *ini)
{
    struct IniSection *i = ini;
    while (i) {
        if (strcmp(i->name, "interfaces") == 0) {
            i = i->next;
            continue;
        }
        if (strcmp(i->name, "objects") == 0) {
            i = i->next;
            continue;
        }
        if (strcmp(i->name, "streams") == 0) {
            i = i->next;
            continue;
        }

        return i->name;
    }

    return NULL;
}

struct R2d2Config *read_config(const char *filename)
{
#define THROW(msg, ...)                                             \
    do {                                                            \
        fprintf(stderr, "config '%s' error: " msg "\n",             \
                 filename, ##__VA_ARGS__);                          \
        delete_inisection(ini);                                     \
        return delete_config(ret);                                  \
    } while (0)

    struct R2d2Config *ret = calloc_struct(R2d2Config);
    struct IniSection *ini = read_inifile(filename);
    if (ini == NULL) {
        THROW("failed to read the ini file");
    }
    remove_comments_from_values(ini);

    struct IniSection *interfaces_sec = inisection_find_section(ini, "interfaces");
    struct IniSection *objects_sec = inisection_find_section(ini, "objects");
    struct IniSection *streams_sec = inisection_find_section(ini, "streams");

    if (interfaces_sec == NULL) {
        THROW("no interfaces section");
    }
    if (streams_sec == NULL) {
        THROW("no streams section");
    }
    // objects are optional

    const char *sec_err = find_duplicate_sections(ini);
    if (sec_err) {
        THROW("section %s defined more than once", sec_err);
    }
    sec_err = find_unknown_section(ini);
    if (sec_err) {
        THROW("unknown section '%s'", sec_err);
    }

    ret->ifaces = parse_interfaces(interfaces_sec, &ret->ifcount);
    if (ret->ifaces == NULL) {
        THROW("interfaces are invalid");
    }

    if (objects_sec) {
        ret->objects = parse_objects(objects_sec);
        if (ret->objects == NULL) {
            THROW("objects are invalid");
        }
    } else {
        // the other stuff expects an existing hash here
        ret->objects = new_hashmap(1, NULL, NULL);
    }

    ret->streams = parse_streams(streams_sec, ret->ifaces, ret->ifcount, ret->objects);
    if (ret->streams == NULL) {
        THROW("streams are invalid");
    }

    ret->iface_streams = parse_interface_streams(interfaces_sec, ret->ifaces, ret->ifcount, ret->streams);
    if (ret->iface_streams == NULL) {
        THROW("interface stream lists are invalid");
    }

    delete_inisection(ini);
    return ret;
}

struct R2d2Config *delete_config(struct R2d2Config *config)
{
    if (!config) return NULL;

    delete_hashmap(config->streams);
    delete_hashmap(config->objects);
    delete_hashmap(config->iface_streams);

    if (config->ifaces) {
        for (unsigned i=0; i<config->ifcount; i++) {
            close_iface(&config->ifaces[i]);
        }
    }
    free(config->ifaces);

    free(config);

    return NULL;
}

struct AddstreamState {
    struct Interface *ifaces;
    unsigned iface_count;
    struct HashMap *pipe_cache;
};

static int addstream_cb(const char *key, void *value, void *userdata)
{
    struct AddstreamState *state = userdata;
    struct ConfStreamList *streamlist = value;

    struct Interface *iface = NULL;
    for (unsigned i=0; i<state->iface_count; i++) {
        if (strcmp(state->ifaces[i].name, key) == 0) {
            iface = &state->ifaces[i];
            break;
        }
    }
    if (iface == NULL) {
        fprintf(stderr, "adding streams to interfaces: unknown interface '%s'\n", key);
        return 0;
    }

    for (struct ConfStreamList *s=streamlist; s; s=s->next) {
        //printf("adding stream %s to interface %s\n", s->stream_name, key);

        struct Pipeline *pipe = hashmap_find(state->pipe_cache, s->stream_name);
        if (pipe) {
            //printf("  reusing already compiled pipeline\n");
        } else {
            //printf("  compiling new pipeline\n");
            unsigned action_count;
            struct Action *actions = assemble_actions(s->stream->actions, &action_count);
            if (!actions) {
                fprintf(stderr, "failed to assemble actions for stream %s\n", s->stream_name);
                return 0;
            }
            pipe = new_pipeline(actions, action_count);
            if (!pipe) { //TODO this never happens
                fprintf(stderr, "failed to create action pipeline for stream %s\n", s->stream_name);
                for (unsigned i=0; i<action_count; i++) {
                    delete_action(actions+i);
                }
                free(actions);
                return 0;
            }
        }

        if (!parsetree_add_stream(iface->parsetree, s->stream->packet, pipe)) {
            fprintf(stderr, "failed to add stream %s to the parsetree of interface %s\n",
                    s->stream_name, key);
            pipeline_unref(pipe); //TODO verify the refcounting scheme, including the error path
            return 0;
        }
        hashmap_insert(state->pipe_cache, strdup(s->stream_name), pipe);
        s->stream->pipeline = pipe;
    }

    return 1;
}

static int pipe_cache_delete_cb(const char *key, void *value, void *userdata)
{
    (void)value;
    (void)userdata;
    free((char*)key);
    return 1;
}

static int del_confactions(const char *key, void *value, void *userdata)
{
    (void)key;
    (void)value;
    (void)userdata;
    struct ConfStream *stream = value;
    stream->actions = delete_confaction_list(stream->actions);
    return 1;
}

bool config_add_streams_to_interfaces(struct R2d2Config *config)
{
    struct AddstreamState state = {
        .ifaces = config->ifaces,
        .iface_count = config->ifcount,
        .pipe_cache = new_hashmap(29, pipe_cache_delete_cb, NULL),
    };
    if (!hashmap_foreach(config->iface_streams, addstream_cb, &state)) {
        fprintf(stderr, "failed to add streams to interfaces\n");
        delete_hashmap(state.pipe_cache);
        return false;
    }
    delete_hashmap(state.pipe_cache);

    // pipeline actions must be independent of the config's ConfAction list
    // we must not segfault if this line is enabled
    // (the only reason to keep the ConfAction list is DynConf's comparison with the new config)
    hashmap_foreach(config->streams, del_confactions, NULL);

    return true;
}


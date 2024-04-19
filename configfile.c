// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "configfile.h"
#include "conf_actions.h"
#include "conf_interface.h"
#include "conf_object.h"
#include "conf_streams.h"
#include "conf_oam.h"
#include "action.h"
#include "inifile.h"
#include "interface.h"
#include "log.h"
#include "parsetree.h"
#include "pipeline.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>

DEFAULT_LOGGING_MODULE(CONFIG, WARNING);

static int remove_comment(const char *key, void *value, void *userdata)
{
    (void)key;
    (void)userdata;
    char *v = (char *)value;
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
        if (strcmp(i->name, "oam") == 0) {
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
        log_error("config '%s' error: " msg,                        \
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
    struct IniSection *oam_sec = inisection_find_section(ini, "oam");

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

    ret->ifaces = parse_interfaces(interfaces_sec);
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

    ret->streams = parse_streams(streams_sec, ret->ifaces, ret->objects);
    if (ret->streams == NULL) {
        THROW("streams are invalid");
    }

    ret->iface_streams = parse_interface_streams(interfaces_sec, ret->ifaces, ret->streams);
    if (ret->iface_streams == NULL) {
        THROW("interface stream lists are invalid");
    }

    if (oam_sec) {
        ret->oam = parse_oam(oam_sec);
        if (ret->oam == NULL) {
            THROW("oam section is invalid");
        }
    } else {
        // the other stuff expects an existing hash here
        ret->oam = new_hashmap(1, NULL, NULL);
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
    delete_hashmap(config->oam);
    delete_hashmap(config->ifaces);
    free(config);

    return NULL;
}

struct AddstreamState {
    struct HashMap *ifaces;
    struct HashMap *pipe_cache;
};

static int addstream_cb(const char *key, void *value, void *userdata)
{
    struct AddstreamState *state = (struct AddstreamState *)userdata;
    struct ConfStreamList *streamlist = (struct ConfStreamList *)value;

    struct Interface *iface = (struct Interface *)hashmap_find(state->ifaces, key);
    if (iface == NULL) {
        log_error("adding streams to interfaces: unknown interface '%s'", key);
        return 0;
    }

    for (struct ConfStreamList *s=streamlist; s; s=s->next) {
        log_info("adding stream %s to interface %s", s->stream_name, key);

        struct Pipeline *pipe = (struct Pipeline *)hashmap_find(state->pipe_cache, s->stream_name);
        if (pipe) {
            log_info("  reusing already compiled pipeline");
        } else {
            log_info("  compiling new pipeline");
            pipe = assemble_actions(s->stream_name, s->stream->actions);
            if (!pipe) {
                log_error("failed to create action pipeline for stream %s", s->stream_name);
                return 0;
            }
        }

        if (!iface_add_stream(iface, s->stream->headers, pipe)) {
            log_error("failed to add stream %s to interface %s",
                    s->stream_name, key);
            pipeline_unref(pipe); //TODO verify the refcounting scheme, including the error path
            return 0;
        }
        hashmap_insert(state->pipe_cache, strdup(s->stream_name), pipe);
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
    struct ConfStream *stream = (struct ConfStream *)value;
    stream->actions = delete_confaction_list(stream->actions);
    return 1;
}

bool config_add_streams_to_interfaces(struct R2d2Config *config)
{
    struct AddstreamState state = {
        .ifaces = config->ifaces,
        .pipe_cache = new_hashmap(29, pipe_cache_delete_cb, NULL),
    };
    if (!hashmap_foreach(config->iface_streams, addstream_cb, &state)) {
        log_error("failed to add streams to interfaces");
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

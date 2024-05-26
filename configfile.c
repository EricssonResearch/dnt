// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "configfile.h"
#include "conf_actions.h"
#include "conf_interface.h"
#include "conf_object.h"
#include "conf_streams.h"
#include "conf_oam.h"
#include "inifile.h"
#include "log.h"
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

struct StateTransaction *read_config_file(const char *filename)
{
#define THROW(msg, ...)                                             \
    do {                                                            \
        log_error("config '%s' error: " msg,                        \
                 filename, ##__VA_ARGS__);                          \
        delete_inisection(ini);                                     \
        return delete_transaction(ret);                             \
    } while (0)

    struct StateTransaction *ret = new_transaction(filename);
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

    if (!parse_interfaces(ret->ifaces, interfaces_sec)) {
        THROW("interfaces are invalid");
    }

    if (objects_sec) {
        if (!parse_objects(ret->objects, objects_sec)) {
            THROW("objects are invalid");
        }
    }

    //TODO only parse streams that are received by an interface
    if (!parse_streams(ret->streams, streams_sec, ret->ifaces, ret->objects)) {
        THROW("streams are invalid");
    }

    if (!parse_interface_streams(ret->iface_streams, interfaces_sec, ret->ifaces, ret->streams)) {
        THROW("interface stream lists are invalid");
    }

    if (oam_sec) {
        if (!parse_oam(ret->oam, oam_sec)) {
            THROW("oam section is invalid");
        }
    }

    delete_inisection(ini);
    return ret;
}


#include "configfile.h"
#include "conf_actions.h"
#include "conf_interface.h"
#include "conf_object.h"
#include "conf_streams.h"
#include "inifile.h"
#include "interface.h"
#include "parsetree.h"
#include "pipeline.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>

struct R2d2Config *read_config(const char *filename)
{
    struct IniSection *ini = read_inifile(filename);
    if (ini == NULL) {
        fprintf(stderr, "failed to read config\n");
        return NULL;
    }

    struct IniSection *interfaces_sec = inisection_find_section(ini, "interfaces");
    struct IniSection *objects_sec = inisection_find_section(ini, "objects");
    struct IniSection *streams_sec = inisection_find_section(ini, "streams");

    if (interfaces_sec == NULL) {
        fprintf(stderr, "config has no interfaces\n");
        return NULL; //TODO cleanup
    }
    if (streams_sec == NULL) {
        fprintf(stderr, "config has no streams\n");
        return NULL;
    }
    // objects are optional

    struct R2d2Config *ret = calloc_struct(R2d2Config);

    ret->ifaces = process_interfaces(interfaces_sec, &ret->ifcount);
    if (ret->ifaces == NULL) {
        fprintf(stderr, "config interfaces invalid\n");
        return NULL;
    }

    if (objects_sec) {
        ret->objects = process_objects(objects_sec);
        if (ret->objects == NULL) {
            //TODO error
        }
    } else {
        ret->objects = new_hashmap(3, NULL, NULL);
    }

    ret->streams = parse_streams(streams_sec, ret->ifaces, ret->ifcount, ret->objects);
    if (ret->streams == NULL) {
        //TODO error
    }

    delete_inisection(ini);
    return ret;
}

static void addstream_cb(const char *key, void *value, void *userdata)
{
    (void)userdata;
    struct ConfStream *stream = value;

    printf("adding stream %s to interface %s\n", key, stream->recv_iface->name);

    unsigned action_count;
    struct Action *actions = assemble_actions(stream->actions, &action_count);
    if (!actions) {
        //TODO error
    }
    struct Pipeline *pipe = new_pipeline(actions, action_count);
    if (!pipe) {
        //TODO error
    }
    parsetree_add_stream(stream->recv_iface->parsetree, stream->packet, pipe);
}

void config_add_streams_to_interfaces(struct R2d2Config *config)
{
    hashmap_foreach(config->streams, addstream_cb, NULL);
}


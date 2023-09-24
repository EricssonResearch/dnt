// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "conf_oam.h"
#include "conf_utils.h"
#include "inifile.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ConfOamState {
    struct HashMap *oam;
};

static int oam_cb(const char *key, void *value, void *userdata)
{
    char *cmdline = value;
    struct ConfOamState *state = userdata;

#ifdef VERBOSE_CONF
    printf("  new OAM session %s -> %s\n", key, cmdline);
#endif

    //TODO we are just copying the hash table...
    hashmap_insert(state->oam, strdup(key), strdup(cmdline));
    return 1;
}

struct HashMap *parse_oam(struct IniSection *oam_section)
{
    struct ConfOamState state = {
        .oam = new_hashmap(7, NULL, NULL),
    };

#ifdef VERBOSE_CONF
    printf("Parsing OAM section:\n");
#endif

    if (!hashmap_foreach(oam_section->contents, oam_cb, &state)) {
        fprintf(stderr, "failed to parse oam\n");
        return NULL;
    }

    return state.oam;
}



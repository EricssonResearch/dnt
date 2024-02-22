// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "conf_oam.h"
#include "conf_utils.h"
#include "inifile.h"
#include "log.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>

DEFAULT_LOGGING_MODULE(CONFIG, WARNING);

struct ConfOamState {
    struct HashMap *oam;
};

static int oam_cb(const char *key, void *value, void *userdata)
{
    char *cmdline = value;
    struct ConfOamState *state = userdata;

    log_info("  OAM background session %s: %s", key, cmdline);

    //TODO we are just copying the hash table...
    hashmap_insert(state->oam, strdup(key), strdup(cmdline));
    return 1;
}

struct HashMap *parse_oam(const struct IniSection *oam_section)
{
    struct ConfOamState state = {
        .oam = new_hashmap(7, NULL, NULL),
    };

    log_info("Parsing OAM section:");

    if (!hashmap_foreach(oam_section->contents, oam_cb, &state)) {
        log_error("failed to parse oam");
        return NULL;
    }

    return state.oam;
}



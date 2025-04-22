// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "conf_oam.h"
#include "inifile.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>

DEFAULT_LOGGING_MODULE(CONFIG, WARNING);

struct ConfOamState {
    struct HashMap *oam;
};

static int oam_cb(const char *key, void *value, void *userdata)
{
    char *cmdline = (char *)value;
    struct ConfOamState *state = (struct ConfOamState *)userdata;

    log_info("  OAM background session %s: %s", key, cmdline);

    //TODO we are just copying the hash table...
    hashmap_insert(state->oam, strdup(key), strdup(cmdline));
    return 1;
}

bool parse_oam(struct HashMap *oam, const struct IniSection *oam_section)
{
    struct ConfOamState state = {
        .oam = oam,
    };

    log_info("Parsing OAM section:");

    if (!hashmap_foreach(oam_section->contents, oam_cb, &state)) {
        log_error("failed to parse oam");
        return false;
    }

    return true;
}



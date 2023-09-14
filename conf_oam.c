// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "conf_oam.h"
#include "conf_streams.h"
#include "conf_actions.h"
#include "conf_packet.h"
#include "conf_utils.h"
#include "inifile.h"
#include "interface.h"
#include "parsetree.h"
#include "utils.h"
#include "oam.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_OAM_SESSIONS    14

struct ConfOamState {
    struct HashMap *oam;
    struct IniSection *oam_section;
    struct HashMap *streams;
};

static int oam_cb(const char *key, void *value, void *userdata)
{
    char *cmdline = value;
    struct ConfOamState *state = userdata;

#ifdef VERBOSE_CONF
    printf("  new OAM session %s -> %s\n", key, cmdline);
#endif

    if(hashmap_count(state->oam) >= MAX_OAM_SESSIONS){
#ifdef VERBOSE_CONF
        printf("  Too many configured OAM sessions. Maximum %d configured sessions are permitted.\n", MAX_OAM_SESSIONS);
#endif
        return 0;
    }
    // ping command
    struct ConfOam *oam = calloc_struct(ConfOam);
    oam->type = COA_PING;
    oam->request = strdup(cmdline);
    oam->name = strdup(key);
    hashmap_insert(state->oam, oam->name, oam);
    return 1;
}
/*
static int checkoam_cb(const char *key, void *value, void *userdata)
{
    struct ConfOamtate *state = userdata;
    (void)value;

    return 1;
}
*/
static int deloam_cb(const char *key, void *value, void *userdata)
{
    (void)userdata;
    printf("Deleting OAM stuff\n");
    struct ConfOam *oam = value;

    free(oam->name);
    free(oam->request);
    delete_confoam(oam);
    free((char*)key);
    return 1;
}

struct HashMap *parse_oam(struct IniSection *oam_section, struct HashMap *streams)
{
    struct ConfOamState state = {
        .oam_section = oam_section,
        .streams = streams,
        .oam = new_hashmap(15, deloam_cb, NULL),
    };

#ifdef VERBOSE_CONF
    printf("Parsing OAM section:\n");
#endif

    if (!hashmap_foreach(oam_section->contents, oam_cb, &state)) {
        fprintf(stderr, "failed to parse oam\n");
        return NULL;
    }
/*  TODO
    // search for stream:mep and mepstop names to see if ping line is valid
    if (!hashmap_foreach(oam_section->contents, checkoam_cb, &state)) {
        fprintf(stderr, "failed to parse oam\n");
        return NULL;
    }

    TODO
    Check that for a stream no more than 14 are configured
*/
    return state.oam;
}

struct ConfOam *delete_confoam(struct ConfOam *oam)
{
    if (!oam) return NULL;

    free(oam);

    return NULL;
}

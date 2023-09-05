// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_CONF_OAM_H
#define R2_CONF_OAM_H

#include "pipeline.h"
struct ConfAction;
struct IniSection;
struct Interface;
struct HashMap;
struct HeaderDescriptor;
struct oam_request;

enum ConfOamType {
    COA_PING = 1,
    COA_RPING,
};

struct ConfOam {
    enum ConfOamType type;
    struct oam_request *request;
    char *name;
};

// @returns hash of ConfOam keyed by oam name
struct HashMap *parse_oam(struct IniSection *streams_section, struct HashMap *streams);

struct ConfOam *delete_confoam(struct ConfOam *oam);

#endif // R2_CONF_OAM_H

// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_CONF_OAM_H
#define R2_CONF_OAM_H

struct IniSection;
struct HashMap;

// @returns hash of the command strings keyed by the name that was in the config
struct HashMap *parse_oam(const struct IniSection *oam_section);

#endif // R2_CONF_OAM_H

// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_CONF_OAM_H
#define R2_CONF_OAM_H

#include "inifile.h"

#include <stdbool.h>

// parses the commands in the config
// @returns false on error
bool parse_oam(struct HashMap *oam, const struct IniSection *oam_section);

#endif // R2_CONF_OAM_H

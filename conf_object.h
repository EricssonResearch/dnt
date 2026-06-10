// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef DNT_CONF_OBJECT_H
#define DNT_CONF_OBJECT_H

#include "inifile.h"

#include <stdbool.h>

// parses the object definitions into @objects
// @returns false on error
bool parse_objects(struct HashMap *objects, const struct IniSection *objects_section);

#endif // DNT_CONF_OBJECT_H



// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_CONF_OBJECT_H
#define R2_CONF_OBJECT_H

#include <stdbool.h>

struct HashMap;
struct IniSection;

// parses the object definitions into @objects
// @returns false on error
bool parse_objects(struct HashMap *objects, const struct IniSection *objects_section);

#endif // R2_CONF_OBJECT_H



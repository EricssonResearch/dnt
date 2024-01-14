// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_CONF_OBJECT_H
#define R2_CONF_OBJECT_H

struct HashMap;
struct IniSection;

// @returns map of PipelineObject keyed by their names
struct HashMap *parse_objects(const struct IniSection *objects_section);

#endif // R2_CONF_OBJECT_H



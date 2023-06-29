// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_CONF_OBJECT_H
#define R2_CONF_OBJECT_H

struct HashMap;
struct IniSection;

enum ConfObjectType {
    CO_SEQGEN = 1,
    CO_SEQREC,
    CO_POF,
};

struct ConfObject {
    enum ConfObjectType type;
    void *object;
};

// @returns map of ConfObject keyed by their names
struct HashMap *parse_objects(struct IniSection *objects_section);

const char *confobject_name_from_type(enum ConfObjectType type);

#endif // R2_CONF_OBJECT_H



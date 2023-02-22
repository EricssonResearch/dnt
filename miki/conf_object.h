
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
    //TODO what else do we need?
};

// @returns map of ConfObject keyed by their names
struct HashMap *process_objects(struct IniSection *objects_section);

#endif // R2_CONF_OBJECT_H



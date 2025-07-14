// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_OBJECT_H
#define R2_OBJECT_H

#include "action.h"
#include "json.h"
#include "hashmap.h"
#include "pipeline.h"

#include <stdbool.h>

enum PipelineObjectType {
    PO_SEQGEN = 1,
    PO_SEQREC,
    PO_POF,
    PO_REPL,
};

// base class for the objects used by the action pipeline
// TODO make the internals private to the object codes
struct PipelineObject {
    char *name;
    enum ActionResult (*process_packet)(struct PipelineObject *self, struct PipelineIterator *pi);
    struct JsonValue *(*get_state)(const struct PipelineObject *self);
    enum PipelineObjectType type;
    int auto_mip_level;
    int reference_count;
};

// add a reference to the object
void pipeline_object_ref(struct PipelineObject *obj);

// remove a reference from the object
// when all references are removed the object deletes itself
void pipeline_object_unref(struct PipelineObject *obj);

// @returns string representation of the @type enum
const char *pipelineobject_name_from_type(enum PipelineObjectType type);

//TODO currently all members of PipelineObject are public...
const char *pipelineobject_get_name(const struct PipelineObject *obj);

// returns a string that is the @json data decoded into a pretty format
// uses @record_sep and @line_sep for formatting
// uses the appropriate printing function based on the type encoded in the @json
// always returns a valid string
char *pipelineobject_sprintf_state_json(struct JsonValue *json, const char *record_sep, const char *line_sep);

#endif // R2_OBJECT_H



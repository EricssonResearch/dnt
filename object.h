// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_OBJECT_H
#define R2_OBJECT_H

#include "action.h"
#include "json.h"

#include <stdbool.h>

enum PipelineObjectType {
    PO_SEQGEN = 1,
    PO_SEQREC,
    PO_POF,
    PO_REPL,
};

struct PipelineIterator;

// base class for the objects used by the action pipeline
struct PipelineObject {
    enum PipelineObjectType type;
    char *name;
    enum ActionResult (*process_packet)(struct PipelineObject *self, struct PipelineIterator *pi);
    struct JsonValue *(*get_state)(const struct PipelineObject *self);
    int auto_mip_level;
};

// uses the delete function for the appropriate type
// always returns NULL
struct PipelineObject *delete_pipeline_object(struct PipelineObject *obj);

// @returns string representation of the @type enum
const char *pipelineobject_name_from_type(enum PipelineObjectType type);

// returns a string that is the @json data decoded into a pretty format
// uses @record_sep and @line_sep for formatting
// uses the appropriate printing function based on the type encoded in the @json
// always returns a valid string
char *sprintf_state_json(struct JsonValue *json, const char *record_sep, const char *line_sep);

#endif // R2_OBJECT_H



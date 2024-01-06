// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_OBJECT_H
#define R2_OBJECT_H

struct JsonValue;

enum PipelineObjectType {
    PO_SEQGEN = 1,
    PO_SEQREC,
    PO_POF,
    PO_REPL,
};

// base class for the objects used by the action pipeline
struct PipelineObject {
    enum PipelineObjectType type;
    char *name;
    //TODO bool (*process_packet)(struct PipelineObject *self, struct PipelineIterator *iter) ?
    struct JsonValue *(*get_state)(const struct PipelineObject *self);
    char *(*sprintf_state_json)(struct JsonValue *json);
};

// uses the delete function for the appropriate type
// always returns NULL
struct PipelineObject *delete_pipeline_object(struct PipelineObject *obj);

// @returns string representation of the @type enum
const char *pipelineobject_name_from_type(enum PipelineObjectType type);

#endif // R2_OBJECT_H



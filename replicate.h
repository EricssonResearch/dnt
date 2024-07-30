// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_REPLICATE_H
#define R2_REPLICATE_H

#include "object.h"
#include "packet.h"

struct PipelineObject *new_replicate(const char *name);

// always returns NULL
struct PipelineObject *delete_replicate(struct PipelineObject *rep);

//TODO receive PipelineIterator instead of packet to have uniform interface with POF
enum ActionResult replicate_packet_passed(struct PipelineObject *rep, struct PipelineIterator *pi);

// use sprintf_state_json() instead of this
char *repl_sprintf_state_json(struct JsonValue *json, const char *record_sep, const char *line_sep);

// store the pipelines in the replication object
void store_replication_pipelines(struct PipelineObject *obj, struct PipelineList *pipes);

struct PipelineList *replicate_get_pipes(struct PipelineObject *rep);

// arguments for try_set_mask callback
struct MaskArg {
    const char *pipename; // pipeline to mask/unmask
    bool new_mask; // true: mask, false: unmask
    bool success;   // true: if pipeline found, false: no pipeline found
};
// set new mask value on a replicaiton object's pipeline if @obj has a pipeline named mask->pipename
// can be used as callback function for @state_foreach_objects()
// @userdata must be a pointer to struct MaskArg
// does nothing if the type of @obj is not PO_REPL
// sets mask->success to true if @obj has a pipeline named mask->pipename
// @returns 0 on success, 1 otherwise
int try_set_mask(struct PipelineObject *obj, void *userdata);

#endif // R2_REPLICATE_H

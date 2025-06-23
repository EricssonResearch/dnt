// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_REPLICATE_H
#define R2_REPLICATE_H

#include "action.h"
#include "object.h"
#include "pipeline.h"


struct PipelineObject *new_replicate(const char *name);

#ifdef OBJECT_INTERNAL
// always returns NULL
struct PipelineObject *delete_replicate(struct PipelineObject *rep);

char *repl_sprintf_state_json(struct JsonValue *json, const char *record_sep, const char *line_sep);
#endif

// store the pipelines in the replication object
void store_replication_pipelines(struct PipelineObject *obj, struct PipelineList *pipes);

struct PipelineList *replicate_get_pipes(struct PipelineObject *rep);

// Check all replication object and for a pipeline with a given @name
// Returns the replication object of the pipeline through @repl if provided
// @return the pipeline if exist or NULL
struct Pipeline *replicate_lookup_pipeline(const char *name, struct PipelineObject **repl);

#endif // R2_REPLICATE_H

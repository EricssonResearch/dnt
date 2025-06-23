// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.

#ifndef R2_POF_H
#define R2_POF_H

#include "object.h"
#include "pipeline.h"

#include <stdbool.h>

// Create new POF object
struct PipelineObject *new_pof(const char *name, unsigned pof_max_delay, unsigned pof_take_any_time, unsigned queue_max_len);

#ifdef OBJECT_INTERNAL
// Delete a POF object
// always returns NULL
struct PipelineObject *delete_pof(struct PipelineObject *pof);

char *pof_sprintf_state_json(struct JsonValue *json, const char *record_sep, const char *line_sep);
#endif

#endif // R2_POF_H

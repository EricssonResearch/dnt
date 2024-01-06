// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.

#ifndef R2_POF_H
#define R2_POF_H

#include "pipeline.h"
#include <stdbool.h>

// Create new POF object
struct PipelineObject *new_pof(const char *name, unsigned pof_max_delay, unsigned pof_take_any_time, unsigned queue_max_len);

// Delete a POF object
// always returns NULL
struct PipelineObject *delete_pof(struct PipelineObject *pof);

// Insert a packet into the buffer of the given POF instance.
// If the buffer is full, return false, otherwise true
bool pof_insert(struct PipelineObject *pof, struct PipelineIterator *pi);

#endif // R2_POF_H

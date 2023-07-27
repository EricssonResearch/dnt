// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.

#ifndef R2_POF_H
#define R2_POF_H

#include "pipeline.h"
#include <stdbool.h>

struct Pof;

// Create new POF object
struct Pof *new_pof(unsigned pof_max_delay, unsigned pof_take_any_time, unsigned queue_max_len);

// Delete a POF object
struct Pof *delete_pof(struct Pof *pof);

// Insert a packet into the buffer of the given POF instance.
// If the buffer is full, return false, otherwise true
bool pof_insert(struct Pof *pof, struct PipelineIterator *pi);

// Return JSON value with the internals of the SeqRecv
struct JsonValue *pof_get_state_json(const void *obj);

#endif // R2_POF_H

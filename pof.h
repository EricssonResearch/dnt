#ifndef R2_POF_H
#define R2_POF_H

#include "action.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>

struct Pof;
struct PofElem;

// Create new POF object
struct Pof *new_pof(unsigned pof_max_delay, unsigned pof_take_any_time, unsigned queue_max_len);

// Delete a POF object
struct Pof *delete_pof(struct Pof *pof);

// Insert a packet into the buffer of the given POF instance.
// If the buffer is full, return false, otherwise true
bool pof_insert(struct Pof *pof, struct PipelineIterator *pi);

#endif // R2_POF_H

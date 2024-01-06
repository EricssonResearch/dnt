// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_SEQ_GEN_H
#define R2_SEQ_GEN_H

#include "object.h"
#include "packet.h"

#include <stdbool.h>

struct PipelineObject *new_seq_gen(const char *name, bool use_reset_flag, bool use_init_flag, unsigned init_seq);

// always returns NULL
struct PipelineObject *delete_seq_gen(struct PipelineObject *gen);

//TODO receive PipelineIterator instead of packet to have uniform interface with POF
void seq_generator(struct PipelineObject *gen, struct Packet *p);

// Helper function to reset all sequence generators in the system
// Triggered by manual reset signal (no timer expiration)
// this is intended to be a hashmap_foreach callback
int reset_all_seq_generators(const char *key, void *value, void *udata);

#endif // R2_SEQ_GEN_H

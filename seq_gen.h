// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_SEQ_GEN_H
#define R2_SEQ_GEN_H

#include "hashmap.h"
#include "header.h"

#include <stdbool.h>

#define FRER_SEQ_GEN_RESET_FLAG_COUNT 3

struct SequenceGenerator;

struct SequenceGenerator *new_seq_gen(bool use_reset_flag, bool use_init_flag, unsigned init_seq);

// always returns NULL
struct SequenceGenerator *delete_seq_gen(struct SequenceGenerator *gen);

// @state is struct SequenceGenerator
void seq_generator(struct SequenceGenerator *gen, struct Packet *p);

// Helper function to reset all sequence generators in the system
// Triggered by manual reset signal (no timer expiration)
int reset_all_seq_generators(const char *key, void *value, void *udata);

// Return JSON value with internals of the SeqGen
struct JsonValue *seqgen_get_state_json(const void *);

#endif // R2_SEQ_GEN_H

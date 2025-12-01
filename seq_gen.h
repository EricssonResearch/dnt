// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_SEQ_GEN_H
#define R2_SEQ_GEN_H

#include "object.h"
#include "packet.h"

#include <stdbool.h>

// Helper function to reset all sequence generators in the system
// Triggered by manual reset signal (no timer expiration)
// this is intended to be a callback for state_foreach_objects()
// does nothing if the type of @obj is not PO_SEQGEN
// doesn't use @userdata
// always @returns 1
int reset_seq_generator(struct PipelineObject *obj, void *userdata);

// the generator implements the reset mechanism described in
//  https://www.ieee802.org/1/files/public/docs2020/new-varga-FRER-seamless-reset-0320-v02.pdf
struct PipelineObject *new_seq_gen(const char *name, bool use_reset_flag, bool use_init_flag, unsigned init_seq);

#ifdef OBJECT_INTERNAL
// always returns NULL
struct PipelineObject *delete_seq_gen(struct PipelineObject *gen);

char *seq_gen_sprintf_state_json(struct JsonValue *json, const char *record_sep, const char *line_sep);
#endif

#endif // R2_SEQ_GEN_H

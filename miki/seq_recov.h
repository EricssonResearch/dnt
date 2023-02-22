
#ifndef R2_SEQ_RECOV_H
#define R2_SEQ_RECOV_H

#include "header.h"

#include <stdbool.h>
#include <stdint.h>

struct SequenceRecovery;

struct SequenceRecovery *new_seq_rec(bool use_reset_flag, bool use_init_flag,
        unsigned history_length, unsigned reset_msec, unsigned latent_error_paths);

// always returns NULL
struct SequenceRecovery *delete_seq_rec(struct SequenceRecovery *rec);

// @returns true if the packet is not a duplicate
bool seq_recovery(struct SequenceRecovery *rec, struct Packet *p, value_producer *read_seq, struct HeaderField *seq);

#endif // R2_SEQ_RECOV_H

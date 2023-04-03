
#ifndef R2_SEQ_RECOV_H
#define R2_SEQ_RECOV_H

#include "header.h"

#include <stdbool.h>
#include <stdint.h>

struct SequenceRecovery;

enum SequenceRecoveryAlgorithm {
    RCVY_Match = 1,
    RCVY_Vector,
    RCVY_SeamlessVector,
};

struct SequenceRecovery *new_seq_rec(enum SequenceRecoveryAlgorithm algo,
        bool use_reset_flag, bool use_init_flag,
        unsigned history_length, unsigned reset_msec, unsigned latent_error_paths);

// always returns NULL
struct SequenceRecovery *delete_seq_rec(struct SequenceRecovery *rec);

// @returns true if the packet is not a duplicate
bool seq_recovery(struct SequenceRecovery *rec, struct Packet *p);

#endif // R2_SEQ_RECOV_H

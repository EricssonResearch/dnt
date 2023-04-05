
#ifndef R2_SEQ_RECOV_H
#define R2_SEQ_RECOV_H

#include <stdbool.h>
#include <stdint.h>

#define FRER_RCVY_SEQ_SPACE (1 << 16)
#define FRER_TICKS_PER_SEC 1000

#define FRER_TTAG_FLAG (1 << (32 - 5))
#define FRER_RESET_FLAG (1 << (32 - 6))
#define FRER_INIT_FLAG (1 << (32 - 7))

struct SequenceRecovery;
struct Packet;

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

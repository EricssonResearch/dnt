// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


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

/*
 * Helper structure to passing and storing diagnostic entity related parameters
 * to the SequenceRecovery instance
 *
 * @latent_error_paths: expected healthy paths at normal operation
 * @latent_error_period: periodicity of latent error testing in millisecs
 * @latent_error_period: periodicity of reseting latent error related counters
 * @latent_error_diff: only signal error, if passed and discarded packet counters
 *  have larger difference than this value (to avoid false positives)
 * @outage_threshold: how large gaps in sequence space determined as an outage
 */
struct RecoveryDiagnosticConf {
    int latent_error_paths;
    int latent_error_period;
    int latent_reset_period;
    int latent_error_difference;
    int outage_threshold;
};

enum SequenceRecoveryAlgorithm {
    RCVY_Match = 1,
    RCVY_Vector,
    RCVY_SeamlessVector,
};

/*
 * Create a new Sequence Recovery instance
 * @algo: the algorithm for seq recovery: match, seamless, vector
 * @use_reset_flag: react to explicit seq reset notification
 * @use_init_flag: use init seq space after reset
 * @history_legth: seq history length
 * @reset_msec: reset after reset_msec millisec if no packet seen
 * @session_id: identifies the ah-hoc created instance for the OAM.
 *              note: must be NULL for non-OAM cases! The instance
 *              self-destruct after reset_msec millisec if session_id != NULL
 */
struct SequenceRecovery *new_seq_rec(enum SequenceRecoveryAlgorithm algo,
        bool use_reset_flag, bool use_init_flag, unsigned history_length,
        unsigned reset_msec, const struct RecoveryDiagnosticConf *diag, const char *session_id);

// always returns NULL
struct SequenceRecovery *delete_seq_rec(struct SequenceRecovery *rec);

// @returns true if the packet is not a duplicate
// @seq is in host byte order
bool seq_recovery(struct SequenceRecovery *rec, unsigned seq);

// Return JSON value with the internals of the SeqRecv
struct JsonValue *seqrec_get_state_json(const void *rec);

/* Return a SeqRecovery for the given key, or create it if not exist.
 * The recommended key is session ID + node ID of the OAM packet
 */
struct SequenceRecovery *get_oam_rcvy(char *key);

#endif // R2_SEQ_RECOV_H

// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_SEQ_RECOV_H
#define R2_SEQ_RECOV_H

#include "object.h"
#include "packet.h"

#include <stdbool.h>
#include <stdint.h>

#define FRER_RCVY_SEQ_SPACE (1 << 16)
#define FRER_TICKS_PER_SEC 1000

#define FRER_TTAG_FLAG (1 << (32 - 5))
#define FRER_RESET_FLAG (1 << (32 - 6))
#define FRER_INIT_FLAG (1 << (32 - 7))


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
 * @name: name of the recovery obj
 * @use_reset_flag: react to explicit seq reset notification
 * @use_init_flag: use init seq space after reset
 * @history_legth: seq history length
 * @reset_msec: reset after reset_msec millisec if no packet seen
 */
struct PipelineObject *new_seq_rec(const char *name, enum SequenceRecoveryAlgorithm algo,
        bool use_reset_flag, bool use_init_flag, unsigned history_length,
        unsigned reset_msec, const struct RecoveryDiagnosticConf *diag);

// always returns NULL
struct PipelineObject *delete_seq_rec(struct PipelineObject *rec);

// @returns ACR_CONTINUE or ACR_DONE whether @pi has a duplicate packet, based on @pi->packet->sequence
// automatically handles OAM packets with temporary recovery objects
enum ActionResult seq_recovery(struct PipelineObject *rec, struct PipelineIterator *pi);

// use sprintf_state_json() instead of this
char *seq_rec_sprintf_state_json(struct JsonValue *json, const char *record_sep, const char *line_sep);

#endif // R2_SEQ_RECOV_H

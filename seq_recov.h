// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef DNT_SEQ_RECOV_H
#define DNT_SEQ_RECOV_H

#include "object.h"
#include "packet.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define FRER_RCVY_SEQ_SPACE (1 << 16)
#define FRER_TICKS_PER_SEC 1000

#define FRER_TTAG_FLAG (1 << (32 - 5))
#define FRER_RESET_FLAG (1 << (32 - 6))
#define FRER_INIT_FLAG (1 << (32 - 7))


/*
 * Helper structure to passing and storing diagnostic entity related parameters
 * to the SequenceRecovery instance
 *
 * @admin_latent_error_paths: initial (user configured) value of latent_error_paths
 * @latent_error_period: periodicity of latent error testing in millisecs
 * @latent_error_period: periodicity of reseting latent error related counters
 * @latent_error_diff: only signal error, if passed and discarded packet counters
 *  have larger difference than this value (to avoid false positives)
 * @outage_threshold: how large gaps in sequence space determined as an outage
 */
struct RecoveryDiagnosticConf {
    int admin_latent_error_paths;
    int latent_error_period;
    int latent_reset_period;
    int latent_error_difference;
    int outage_threshold;
};

// SeamlessVector implements this:
//  https://www.ieee802.org/1/files/public/docs2020/new-varga-FRER-seamless-reset-0320-v02.pdf
enum SequenceRecoveryAlgorithm {
    RCVY_Match = 1,
    RCVY_Vector,
    RCVY_SeamlessVector,
};


void seq_rec_register_preAutoMIP(struct PipelineObject *obj, const char *mip_name);

void seq_rec_register_postAutoMIP(struct PipelineObject *obj, const char *mip_name);

// set one recovery path to "masked" state: the replication intentionally won't send on this path
// @returns false on error
bool seq_rec_path_masked(struct PipelineObject *obj, const char *mip_name);

// clears the "masked" state on one recovery path
// @returns false on error
bool seq_rec_path_unmasked(struct PipelineObject *obj, const char *mip_name);

// prints masking status to @cmd_w
void seq_rec_report_mask_state(struct PipelineObject *obj, FILE *cmd_w);

// sequence recovery for OAM messages
enum ActionResult oam_recovery(struct PipelineObject *obj, struct Packet *p, const char *session_id, unsigned char seq);

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

#ifdef OBJECT_INTERNAL
// always returns NULL
struct PipelineObject *delete_seq_rec(struct PipelineObject *rec);

char *seq_rec_sprintf_state_json(struct JsonValue *json, const char *record_sep, const char *line_sep);
#endif

#ifdef TESTING

struct SequenceRecovery *get_oam_rcvy(struct PipelineObject *rec, const char *session_id);

#endif

#endif // DNT_SEQ_RECOV_H

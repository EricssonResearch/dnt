// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.

#define OBJECT_INTERNAL

#include "seq_recov.h"
#include "action.h"
#include "json.h"
#include "log.h"
#include "notification.h"
#include "oam.h"
#include "packet.h"
#include "pipeline.h"
#include "time_utils.h"
#include "thread_utils.h"
#include "utils.h"

#include <string.h>
#include <stdlib.h>

#include <arpa/inet.h>
#include <time.h>

#define OAM_RCVY_RESET_MS 5000

#ifdef TESTING
#define TESTABLE
#else
#define TESTABLE static
#endif

DEFAULT_LOGGING_MODULE(RCVY, WARNING);
LOGGING_MODULE(DIAGNOSTIC, WARNING);

struct SequenceRecovery {
    struct PipelineObject base;

    enum SequenceRecoveryAlgorithm algorithm;
    struct RecoveryDiagnosticConf diag;
    bool use_reset_flag;
    bool use_init_flag;
    bool individual_recovery;
    int history_length;
    unsigned reset_msec;

    unsigned recv_seq;
    unsigned init_recv_seq;
    int cur_base_difference;
    char *history;
    char *init_history;
    bool take_any;
    bool init_take_any;
    bool was_star;

    int lost_packets;
    int out_of_order_packets;
    int passed_packets;
    int passed_packets_last;
    int rogue_packets;
    int rogue_packets_last;
    int discarded_packets;
    int discarded_packets_last;
    int remaining_ticks;
    int seq_recovery_resets;
    int latent_errors;
    int latent_reset_counter;
    int latent_error_counter;
    int latent_error_resets;
    /* for loss spike detection */
    int consecutive_loss;
    int consecutive_loss_max;
    int skip_loss_after_reset_guard;

    struct Thread *reset_thread;

    struct HashMap *oam_seq_recoveries; // session_id -> struct SequenceRecovery
    char *session_id; // for OAM only
};

static int oam_rcvy_del_cb(const char *key, void *value, void *userdata)
{
    (void)key;
    (void)userdata;
    struct PipelineObject *rec = (struct PipelineObject *)value;
    //free((char*)key); this is rec->session_id
    pipeline_object_unref(rec);
    return 1;
}

TESTABLE struct SequenceRecovery *get_oam_rcvy(struct PipelineObject *obj, const char *session_id)
{
    struct SequenceRecovery *rec = (struct SequenceRecovery *)obj;
    struct SequenceRecovery *oamrec = NULL;

    if (rec->oam_seq_recoveries == NULL) {
        rec->oam_seq_recoveries = new_hashmap(5, oam_rcvy_del_cb, NULL);
    } else {
        oamrec = (struct SequenceRecovery *)hashmap_find(rec->oam_seq_recoveries, session_id);
    }

    if (oamrec == NULL) {
        struct PipelineObject *r = new_seq_rec(session_id, RCVY_Match, false, false, 0, OAM_RCVY_RESET_MS, NULL);
        if (r == NULL) {
            // most likely we couldn't launch the reset thread
            return NULL;
        }
        oamrec = (struct SequenceRecovery *)r;
        oamrec->session_id = strdup(session_id);
        oamrec->oam_seq_recoveries = rec->oam_seq_recoveries; // need to point to the parent's hash
        hashmap_insert(rec->oam_seq_recoveries, oamrec->session_id, oamrec);
    }
    return oamrec;
}

static void reset_ticks(struct SequenceRecovery *rec)
{
    rec->remaining_ticks = ((rec->reset_msec * FRER_TICKS_PER_SEC) + 999) / 1000;
}

static struct JsonValue *get_state_json(const struct PipelineObject *obj)
{
    // TODO: print OAM match recovery child's status too
    const struct SequenceRecovery *rec = (const struct SequenceRecovery *)obj;
    struct JsonValue *js = json_object();
    json_object_insert(js, "type", json_string("seqrec"));
    json_object_insert(js, "name", json_string(obj->name));

    const char *algo = NULL;
    switch (rec->algorithm) {
        case RCVY_Match: algo = "match"; break;
        case RCVY_Vector: algo = "vector"; break;
        case RCVY_SeamlessVector: algo = "seamless_vector"; break;
    }
    json_object_insert(js, "recovery_algorithm", json_string(algo));

    json_object_insert(js, "reset_msec", json_number((double) rec->reset_msec));
    json_object_insert(js, "recovery_seq_num", json_number((double) rec->recv_seq));
    json_object_insert(js, "passed_packets", json_number((double) rec->passed_packets));
    json_object_insert(js, "discarded_packets", json_number((double) rec->discarded_packets));
    json_object_insert(js, "seq_recovery_resets", json_number((double) rec->seq_recovery_resets));

    if (rec->algorithm != RCVY_Match) { //only for vector & seamless
        json_object_insert(js, "use_init_flag", rec->use_init_flag ? json_true() : json_false());
        json_object_insert(js, "use_reset_flag", rec->use_reset_flag ? json_true() : json_false());
        json_object_insert(js, "history_length", json_number((double) rec->history_length));
        json_object_insert(js, "latent_error_paths", json_number((double) rec->diag.latent_error_paths));
        json_object_insert(js, "latent_error_resets", json_number((double) rec->latent_error_resets));
        json_object_insert(js, "latent_errors", json_number((double) rec->latent_errors));

        if (log_enabled(DEBUG)) {
            char *hist_content = (char *)calloc(1, rec->history_length + 1);
            for (int i = 0; i < rec->history_length; ++i) {
                if (rec->history[i] == 1)
                    hist_content[i] = '1';
                else if (rec->history[i] == 0)
                    hist_content[i] = '0';
            }
            json_object_insert(js, "history", json_string(hist_content));
            free(hist_content);
        }
    }
    return js;
}


static NotificationLevel seq_rec_notification_pull_fn(void *self, struct JsonValue **msg)
{
    struct PipelineObject *rep = (struct PipelineObject *)self;
    struct JsonValue *js = get_state_json(rep);
    *msg = js;
    return NOTIF_PULL;
}

static void shift_seq_history(struct SequenceRecovery *rec, char *history,  unsigned new_zero)
{
    if (history[rec->history_length - 1] == 0) {
        if (rec->skip_loss_after_reset_guard == 0) {
            rec->lost_packets += 1;
            rec->consecutive_loss += 1;
            if (rec->consecutive_loss > rec->consecutive_loss_max) {
                rec->consecutive_loss_max = rec->consecutive_loss;
            }
        } else {
            rec->skip_loss_after_reset_guard -= 1;
        }
    } else {
        rec->consecutive_loss = 0;
    }
    for(int i = rec->history_length - 1; i != 0; --i)
        history[i] = history[i - 1];
    history[0] = new_zero;
}

static inline int calc_delta(int seq1, int seq2)
{
    int delta = (seq1 - seq2) & (FRER_RCVY_SEQ_SPACE - 1);
    if((delta & (FRER_RCVY_SEQ_SPACE / 2)) != 0)
        delta = delta - FRER_RCVY_SEQ_SPACE;
    return delta;
}

static bool recover(struct SequenceRecovery *rec, unsigned packet_seq, bool init)
{
    unsigned int *recovery_seq;
    bool *take_any;
    char *history;

    if (init) {
        recovery_seq = &rec->init_recv_seq;
        take_any = &rec->init_take_any;
        history = rec->init_history;
    } else {
        recovery_seq = &rec->recv_seq;
        take_any = &rec->take_any;
        history = rec->history;
    }

    int delta = calc_delta(packet_seq, *recovery_seq);
    if(*take_any) {
        *take_any = false;
        history[0] = 1;
        *recovery_seq = packet_seq;
        rec->passed_packets += 1;
        reset_ticks(rec);
        return true;
    } else if(delta > rec->history_length || delta <= -rec->history_length) {
        rec->rogue_packets += 1;
        rec->discarded_packets += 1;

        if(rec->individual_recovery)
            reset_ticks(rec);
    } else if(delta <= 0) {
        if(history[-delta] == 0) {
            history[-delta] = 1;
            rec->out_of_order_packets += 1;
            rec->passed_packets += 1;
            reset_ticks(rec);
            return true;
        } else {
            rec->discarded_packets += 1;
            if(rec->individual_recovery)
                reset_ticks(rec);
        }
    } else {
        if(delta != 1)
            rec->out_of_order_packets += 1;

        while((delta -= 1) != 0)
            shift_seq_history(rec, history, 0);
        shift_seq_history(rec, history, 1);
        *recovery_seq = packet_seq;
        rec->passed_packets += 1;
        reset_ticks(rec);
        return true;
    }
    return false;
}

static bool match_seq_recovery(struct SequenceRecovery *rec, unsigned seq)
{
    /* unsigned flags = ntohl(p->sequence) & 0xffff0000; */
    seq = seq & 0xffff;
    if (rec->take_any) {
        rec->take_any = false;
        rec->recv_seq = seq;

        rec->passed_packets += 1;
        reset_ticks(rec);
        return true;
    }

    int delta = (seq - rec->recv_seq) & (FRER_RCVY_SEQ_SPACE - 1);
    if (delta == 0) {
        rec->discarded_packets += 1;
        if (rec->individual_recovery)
            reset_ticks(rec);
    } else {
        if (delta != 1) {
            rec->out_of_order_packets += 1;
        }
        //TODO: use atomic, no lock required
        rec->recv_seq = seq;
        rec->passed_packets += 1;
        reset_ticks(rec);
        return true;
    }

    return false;
}

static bool vector_seq_recovery(struct SequenceRecovery *rec, unsigned seq)
{
    unsigned packet_seq = seq & 0xffff;
    return recover(rec, packet_seq, false);
}

static bool seamless_seq_recovery(struct SequenceRecovery *rec, unsigned seq)
{
    int delta = 0;
    // TODO: use proper metadata seq/flags as packet member not rtag format
    unsigned flags = seq & 0xffff0000;
    seq = seq & 0xffff;
    if(rec->use_reset_flag) {
        if(rec->use_init_flag) {
            if(flags & FRER_INIT_FLAG) {

                delta = calc_delta(seq, rec->init_recv_seq);
                if(flags & FRER_RESET_FLAG) {
                    if((delta > rec->history_length) || (delta <= -rec->history_length * 2))
                        rec->init_take_any = true;
                }

                int high = FRER_RCVY_SEQ_SPACE - 3 * rec->history_length;
                int low = FRER_RCVY_SEQ_SPACE - rec->history_length;

                if((int)seq >= high && (int)seq < low)
                    rec->take_any = true;
                return recover(rec, seq, true);
            } // init flag set in packet
        } // disabled init flag handling

        if(flags & FRER_RESET_FLAG) {
            delta = calc_delta(seq, rec->recv_seq);
            if((delta > rec->history_length) || (delta <= -rec->history_length * 2))
                rec->take_any = true;
        }
    } // no reset flag handling
    return recover(rec, seq, false);
}

static void vector_seq_recovery_reset(struct SequenceRecovery *rec)
{
    rec->recv_seq = FRER_RCVY_SEQ_SPACE - 1;
    memset(rec->history, 0, rec->history_length * sizeof(char));
}

static void seamless_seq_recovery_reset(struct SequenceRecovery *rec)
{
    vector_seq_recovery_reset(rec);
    memset(rec->init_history, 0, rec->history_length * sizeof(char));
    rec->init_recv_seq = FRER_RCVY_SEQ_SPACE - 1;
    rec->init_take_any = true;
}

//TODO: race condition
static enum ActionResult seq_recovery(struct PipelineObject *r, struct PipelineIterator *pi)
{
    struct SequenceRecovery *rec = (struct SequenceRecovery *)r;
    struct Packet *p = pi->packet;
    bool accept = true;
    uint32_t seq = ntohl(p->sequence);
    //TODO grab mutex
    switch (rec->algorithm) {
        case RCVY_Vector:
            accept = vector_seq_recovery(rec, seq);
            break;
        case RCVY_SeamlessVector:
            accept = seamless_seq_recovery(rec, seq);
            break;
        case RCVY_Match:
            accept = match_seq_recovery(rec, seq);
            break;
    }
    //TODO release mutex
    if (accept){
        packet_logcat(p, "(%d pass) ", seq);
    } else {
        packet_logcat(p, "(%d drop) ", seq);
    }
    return accept ? ACR_CONTINUE : ACR_DONE;
}

static void seq_recovery_reset(struct SequenceRecovery *rec)
{
    log_info("%s%s: Sequence recovery reset.", rec->session_id ? "(OAM)" : "", rec->base.name);
    struct JsonValue *noti = json_object();
    json_object_insert(noti, "source", json_string(rec->base.name));
    json_object_insert(noti, "message", json_string("recovery reset"));
    notification_push_event("seq_rcvy", NOTIF_INFO, noti);
    rec->seq_recovery_resets += 1;
    rec->take_any = true;
    switch (rec->algorithm) {
        case RCVY_Match:
            break;
        case RCVY_Vector:
            return vector_seq_recovery_reset(rec);
        case RCVY_SeamlessVector:
            return seamless_seq_recovery_reset(rec);
    }
}

static void recovery_diagnostic(struct SequenceRecovery *rec)
{
#define ALERT(msg, ...)                                                     \
    do {                                                                    \
        log_warning_m(DIAGNOSTIC, msg, ##__VA_ARGS__);                      \
        oam_cli_alert(msg, ##__VA_ARGS__);                                  \
        char *notistr = strdup_printf(msg, ##__VA_ARGS__);                  \
        struct JsonValue *noti = json_object();                             \
        json_object_insert(noti, "source", json_string(rec->base.name));    \
        json_object_insert(noti, "alert", json_string(notistr));            \
        notification_push_event("diagnostic", NOTIF_WARNING, noti);         \
        free(notistr);                                                      \
    } while (0)                                                             \

    const char *fmt_more = "%s: MORE_PACKETS_THAN_EXPECTED with %d percent";
    const char *fmt_absent = "%s: PACKET_ABSENT";
    const char *fmt_disfunct = "%s: DISFUNCTIONING_PATHS: %d path(s)";
    const char *fmt_disfunct_absent = "%s: DISFUNCTIONING_PATHS: %d path(s) and PACKET_ABSENT";
    const char *fmt_outofwin = "%s: OUTOFWINDOW_PACKETS: %d";
    const char *fmt_loss = "%s: PACKET_LOSS: %d consecutive packets and %d aggregate lost";

    const struct RecoveryDiagnosticConf *diag = &rec->diag;
    int diff, discarded_diff, passed_diff;
    int disfunctioning_paths;
    if (rec->take_any)
        return;
    discarded_diff = rec->discarded_packets - rec->discarded_packets_last;
    passed_diff = rec->passed_packets - rec->passed_packets_last;
    if (passed_diff == 0)
        return;
    diff = discarded_diff - ((diag->latent_error_paths - 1) * passed_diff);
    unsigned int working_ceil = (discarded_diff + diag->latent_error_difference - 1) / passed_diff;
    unsigned int working_floor = (discarded_diff - diag->latent_error_difference) / passed_diff;
    if (diff > -diag->latent_error_difference && diff < diag->latent_error_difference) {
        goto update;
    } else if (diff >= diag->latent_error_difference) {
        int more_percent = (discarded_diff * 100) / ((diag->latent_error_paths - 1) * passed_diff);
        ALERT(fmt_more, rec->base.name, more_percent);
        goto update;
    }
    if (discarded_diff < diag->latent_error_difference) {
        disfunctioning_paths = diag->latent_error_paths - 1;
        ALERT(fmt_disfunct, rec->base.name, disfunctioning_paths);
    } else if (working_ceil == working_floor) {
        disfunctioning_paths = diag->latent_error_paths - 2 - working_ceil;
        if (disfunctioning_paths > 0) {
            ALERT(fmt_disfunct_absent, rec->base.name, disfunctioning_paths);
        } else {
            ALERT(fmt_absent, rec->base.name);
        }
    } else {
        disfunctioning_paths = diag->latent_error_paths - 1 - working_ceil;
        ALERT(fmt_disfunct, rec->base.name, disfunctioning_paths);
    }
    if (rec->rogue_packets != rec->rogue_packets_last) {
        ALERT(fmt_outofwin, rec->base.name, rec->rogue_packets);
        rec->rogue_packets_last = rec->rogue_packets;
    }
    if (rec->consecutive_loss_max > diag->outage_threshold) {
        ALERT(fmt_loss, rec->base.name, rec->consecutive_loss_max, rec->lost_packets);
        rec->consecutive_loss_max = 0;
    }
update:
    rec->discarded_packets_last = rec->discarded_packets;
    rec->passed_packets_last = rec->passed_packets;
}

static void latent_error_test(struct SequenceRecovery *rec)
{
    int diff = rec->cur_base_difference - ((rec->passed_packets * (rec->diag.latent_error_paths - 1)) - rec->discarded_packets);
    if (rec->diag.latent_error_paths > 1 && rec->diag.latent_error_period > 0) {
        if (diff < 0)
            diff = -diff;
        if (diff > rec->diag.latent_error_difference) {
            log_warning_m(DIAGNOSTIC, "%s: Latent error signal", rec->base.name);
            rec->latent_errors += 1;
        }
        recovery_diagnostic(rec);
    }
}

static void latent_error_reset(struct SequenceRecovery *rec)
{
    rec->cur_base_difference = (rec->passed_packets * (rec->diag.latent_error_paths - 1)) - rec->discarded_packets;
    rec->latent_error_resets += 1;
    rec->skip_loss_after_reset_guard = rec->history_length - 1;
}

// return false to stop reset thread
static bool decrement_ticks(struct SequenceRecovery *rec)
{
    rec->latent_reset_counter += 1000 / FRER_TICKS_PER_SEC;
    if (rec->diag.latent_reset_period && (rec->latent_reset_counter >= rec->diag.latent_reset_period)) {
        latent_error_reset(rec);
        rec->latent_reset_counter = 0;
    }
    rec->latent_error_counter += 1000 / FRER_TICKS_PER_SEC;
    if (rec->latent_error_counter >= rec->diag.latent_error_period) {
        latent_error_test(rec);
        rec->latent_error_counter = 0;
    }
    if (rec->remaining_ticks == 0)
        return true;
    rec->remaining_ticks -= 1;
    if (rec->remaining_ticks == 0) {
        seq_recovery_reset(rec);
        // oam sequence recovery dies upon reset
        if (rec->session_id)
            return false; // stop reset thread
    }
    return true;
}

// This thread decrement the @remaining_ticks periodically
// and check do a Sequence Recovery reset if it reach zero.
static void *reset_thread(void *arg)
{
    struct SequenceRecovery *rec = (struct SequenceRecovery *)arg;
    struct timespec sleep_until, delta, now;

    //TODO grab mutex

    reset_ticks(rec);

    const time_t tick_ns = NSEC_PER_SEC / FRER_TICKS_PER_SEC;
    delta.tv_sec = tick_ns / NSEC_PER_SEC;
    delta.tv_nsec = tick_ns % NSEC_PER_SEC;
    clock_gettime(CLOCK_REALTIME, &now);
    timespecadd(&now, &delta, &sleep_until);
    bool should_run = true;
    while (should_run) {
        clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &sleep_until, NULL);
        clock_gettime(CLOCK_REALTIME, &now);
        if (timespeccmp(&now, &sleep_until, <)) {
            log_debug("%s: Early wakeup. continue sleep...", rec->base.name);
            // Unlikely early wake up, continue with sleeping
            continue;
        }
        should_run = decrement_ticks(rec);
        timespecadd(&now, &delta, &sleep_until);
    }
    struct Thread *reset_thread = rec->reset_thread;
    rec->reset_thread = NULL;
    // here we use our pointer to the parent's hash
    hashmap_remove(rec->oam_seq_recoveries, rec->session_id);
    thread_exit(reset_thread);
    return rec;
}

void seq_rec_set_latent_error_paths(struct PipelineObject *obj, int paths)
{
    struct SequenceRecovery *rec = (struct SequenceRecovery *)obj;
    if (paths < 0) {
        log_error("%s: OAM detected negative available paths: %d", obj->name, paths);
        return;
    }
    if (paths > rec->diag.admin_latent_error_paths) {
        log_error("%s: OAM detected more paths (%d) than configured (%d)", obj->name,
                  paths, rec->diag.admin_latent_error_paths);
        return;
    }
    rec->diag.latent_error_paths = paths;
    rec->diag.invalid = true;
    latent_error_reset(rec);
}

enum ActionResult oam_recovery(struct PipelineObject *obj, struct Packet *p, const char *session_id, unsigned char seq)
{
    struct SequenceRecovery *oam_rec = get_oam_rcvy(obj, session_id);
    bool accept = true;

    if (oam_rec) {
        accept = match_seq_recovery(oam_rec, seq);
    }
    if (accept) {
        packet_logcat(p, "(OAM %d pass) ", seq);
    } else {
        packet_logcat(p, "(OAM %d drop) ", seq);
    }

    return accept ? ACR_CONTINUE : ACR_DONE;
}

struct PipelineObject *new_seq_rec(const char *name, enum SequenceRecoveryAlgorithm algo,
        bool use_reset_flag, bool use_init_flag,
        unsigned history_length, unsigned reset_msec,
        const struct RecoveryDiagnosticConf *diag)
{
    struct SequenceRecovery *ret = calloc_struct(SequenceRecovery);

    ret->base.type = PO_SEQREC;
    ret->base.name = strdup(name);
    ret->base.get_state = get_state_json;
    ret->base.process_packet = seq_recovery;
    ret->base.reference_count = 1;

    ret->algorithm = algo;
    ret->use_reset_flag = use_reset_flag;
    ret->use_init_flag = use_init_flag;
    ret->history_length = history_length;
    ret->reset_msec = reset_msec;
    if (diag) {
        ret->diag = *diag;
    }
    ret->history = (char *)calloc(history_length, sizeof(char)); //TODO not if algo==Match
    ret->init_history = (char *)calloc(history_length, sizeof(char)); //TODO we only need this when algo==Seamless
    ret->take_any = true;
    ret->init_take_any = true;
    ret->session_id = NULL;

    ret->reset_thread = thread_launch(reset_thread, ret, "sqrst %s", name);
    if (ret->reset_thread == NULL) {
        log_error("cant't create reset thread for %s", name);
        return delete_seq_rec((struct PipelineObject *)ret);
    }
    notification_register_source(name, seq_rec_notification_pull_fn, ret, 2000);

    return (struct PipelineObject *)ret;
}

struct PipelineObject *delete_seq_rec(struct PipelineObject *r)
{
    struct SequenceRecovery *rec = (struct SequenceRecovery *)r;
    notification_register_source(r->name, NULL, NULL, 2000);
    rec->reset_thread = thread_stop(rec->reset_thread);
    if (rec->session_id == NULL) // oam rcvy points to its parent's hash
        delete_hashmap(rec->oam_seq_recoveries);
    free(rec->history);
    free(rec->init_history);
    free(rec->session_id);
    free(r->name);
    free(rec);
    return NULL;
}

char *seq_rec_sprintf_state_json(struct JsonValue *json, const char *record_sep, const char *line_sep)
{
    struct JsonValue *algorithm = json_object_get_string(json, "recovery_algorithm");
    struct JsonValue *reset_msec = json_object_get_number(json, "reset_msec");
    struct JsonValue *recovery_seq_num = json_object_get_number(json, "recovery_seq_num");
    struct JsonValue *passed_packets = json_object_get_number(json, "passed_packets");
    struct JsonValue *discarded_packets = json_object_get_number(json, "discarded_packets");
    struct JsonValue *seq_recovery_resets = json_object_get_number(json, "seq_recovery_resets");

    if (algorithm && reset_msec && recovery_seq_num && passed_packets && discarded_packets && seq_recovery_resets) {
        if (strcmp(algorithm->v.string, "match") != 0) {
            struct JsonValue *use_init_flag = json_object_get_bool(json, "use_init_flag");
            struct JsonValue *use_reset_flag = json_object_get_bool(json, "use_reset_flag");
            struct JsonValue *history_length = json_object_get_number(json, "history_length");
            struct JsonValue *latent_error_paths = json_object_get_number(json, "latent_error_paths");
            struct JsonValue *latent_error_resets = json_object_get_number(json, "latent_error_resets");
            struct JsonValue *latent_errors = json_object_get_number(json, "latent_errors");
            struct JsonValue *history = json_object_get_string(json, "history");

            if (use_init_flag && use_reset_flag && history_length &&
                    latent_error_paths && latent_error_resets && latent_errors) {
                return strdup_printf("recovery_algorithm %s%sreset_timer %.0fms%s"
                        "use_init_flag %s%suse_reset_flag %s%shistory_length %.0f%s"
                        "history_content %s%s"
                        "latent_error_paths %.0f%slatent_error_resets %.0f%slatent_errors %.0f%s"
                        "latest_valid_sequence_number %.0f%spassed %.0f%sdiscarded %.0f%s"
                        "number_of_resets %.0f",
                        algorithm->v.string, record_sep, reset_msec->v.number, line_sep,
                        (use_init_flag->type == JSON_TRUE) ? "true" : "false", record_sep,
                        (use_reset_flag->type == JSON_TRUE) ? "true" : "false", record_sep,
                        history_length->v.number, line_sep,
                        history ? history->v.string : "...", line_sep,
                        latent_error_paths->v.number, record_sep, latent_error_resets->v.number, record_sep,
                        latent_errors->v.number, line_sep,
                        recovery_seq_num->v.number, record_sep,
                        passed_packets->v.number, record_sep, discarded_packets->v.number, line_sep,
                        seq_recovery_resets->v.number);
            } else {
                return strdup_printf("<invalid seq_rec %s state>", algorithm->v.string);
            }
        } else {
            return strdup_printf("recovery_algorithm %s%sreset_timer %.0fms%s"
                    "latest_valid_sequence_number %.0f%spassed %.0f%sdiscarded %.0f%s"
                    "number_of_resets %.0f",
                    "match", record_sep, reset_msec->v.number, line_sep,
                    recovery_seq_num->v.number, record_sep,
                    passed_packets->v.number, record_sep, discarded_packets->v.number, line_sep,
                    seq_recovery_resets->v.number);
        }
    } else {
        return strdup("<invalid seq_rec state>");
    }
}


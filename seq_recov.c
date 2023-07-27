// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.

#include "seq_recov.h"
#include "oam.h"
#include "time_utils.h"
#include "utils.h"
#include "packet.h"
#include "json.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#include <arpa/inet.h>
#include <time.h>

#define OAM_RCVY_RESET_MS 5000

struct SequenceRecovery {
    enum SequenceRecoveryAlgorithm algorithm;
    bool use_reset_flag;
    bool use_init_flag;
    bool individual_recovery;
    int history_length;
    unsigned reset_msec;
    unsigned latent_error_paths;
    int latent_error_period;
    int latent_reset_period;
    int latent_error_difference;

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
    int rogue_packets;
    int discarded_packets;
    int remaining_ticks;
    int seq_recovery_resets;
    int latent_errors;
    int latent_reset_counter;
    int latent_error_counter;
    int latent_error_resets;

    pthread_t reset_thread;
    char *session_id; // for OAM only
};

// TODO: make struct OamSession if more per-session info needed for MEP/MIP.
// currently the only state of the session is the seq recovery
static struct HashMap *oam_seq_recoveries = NULL; // session_id -> struct SequenceRecovery

struct SequenceRecovery *get_oam_rcvy(char *key)
{
    if (oam_seq_recoveries == NULL)
        oam_seq_recoveries = new_hashmap(51, NULL, NULL);
    struct SequenceRecovery *rec = hashmap_find(oam_seq_recoveries, key);
    if (rec == NULL) {
        rec = new_seq_rec(RCVY_Match, false, false, 0, OAM_RCVY_RESET_MS, 0, key);
        hashmap_insert(oam_seq_recoveries, key, rec);
    }
    return rec;
}

void delete_oam_rcvy(char *key)
{
    struct SequenceRecovery *rec = hashmap_find(oam_seq_recoveries, key);
    if (rec) {
        hashmap_remove(oam_seq_recoveries, key);
        delete_seq_rec(rec);
    }
}

static void *reset_thread(void *arg);

static void reset_ticks(struct SequenceRecovery *rec)
{
    rec->remaining_ticks = ((rec->reset_msec * FRER_TICKS_PER_SEC) + 999) / 1000;
}

struct SequenceRecovery *new_seq_rec(enum SequenceRecoveryAlgorithm algo, bool use_reset_flag, bool use_init_flag,
        unsigned history_length, unsigned reset_msec, unsigned latent_error_paths, const char *session_id)
{
    struct SequenceRecovery *ret = calloc_struct(SequenceRecovery);

    ret->algorithm = algo;
    ret->use_reset_flag = use_reset_flag;
    ret->use_init_flag = use_init_flag;
    ret->history_length = history_length;
    ret->reset_msec = reset_msec;
    ret->latent_error_paths = latent_error_paths;
    ret->history = calloc(history_length, sizeof(char)); //TODO not if algo==Match
    ret->init_history = calloc(history_length, sizeof(char)); //TODO we only need this when algo==Seamless
    ret->take_any = true;
    ret->init_take_any = true;
    if (session_id)
        ret->session_id = strdup(session_id);
    pthread_create(&ret->reset_thread, NULL, reset_thread, ret);

    return ret;
}

struct SequenceRecovery *delete_seq_rec(struct SequenceRecovery *rec)
{
    pthread_cancel(rec->reset_thread);
    pthread_join(rec->reset_thread, NULL);
    free(rec->history);
    free(rec->init_history);
    free(rec);
    return NULL;
}

static void shift_seq_history(struct SequenceRecovery *rec, char *history,  unsigned new_zero)
{
    if(history[rec->history_length - 1] == 0)
        rec->lost_packets += 1;
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
    } else if(delta >= rec->history_length || delta <= -rec->history_length) {
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
bool seq_recovery(struct SequenceRecovery *rec, unsigned seq)
{
    bool ret = true;
    //TODO grab mutex
    switch (rec->algorithm) {
        case RCVY_Vector:
            ret = vector_seq_recovery(rec, seq);
            break;
        case RCVY_SeamlessVector:
            ret = seamless_seq_recovery(rec, seq);
            break;
        case RCVY_Match:
            ret = match_seq_recovery(rec, seq);
            break;
    }
    //TODO release mutex
    return ret;
}

static void seq_recovery_reset(struct SequenceRecovery *rec)
{
    printf("Sequence recovery reset. %s\n", rec->session_id ? "(OAM)" : "");
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

static void latent_error_test(struct SequenceRecovery *rec)
{
    int diff = rec->cur_base_difference - (rec->passed_packets * (rec->latent_error_paths - 1)) - rec->discarded_packets;
    if (rec->latent_error_paths > 1 && rec->latent_error_period > 0) {
        if (diff < 0)
            diff = -diff;
        if (diff > rec->latent_error_difference) {
            printf("Latent Error Signal\n");
            rec->latent_errors += 1;
        }
    }
}

static void latent_error_reset(struct SequenceRecovery *rec)
{
    rec->cur_base_difference = (rec->passed_packets * (rec->latent_error_paths - 1)) - rec->discarded_packets;
    rec->latent_error_resets += 1;
}

static void decrement_ticks(struct SequenceRecovery *rec)
{
    rec->latent_reset_counter += 1000 / FRER_TICKS_PER_SEC;
    if (rec->latent_reset_counter >= rec->latent_reset_period) {
        latent_error_reset(rec);
        rec->latent_reset_counter = 0;
    }
    rec->latent_error_counter += 1000 / FRER_TICKS_PER_SEC;
    if (rec->latent_error_counter >= rec->latent_error_period) {
        latent_error_test(rec);
        rec->latent_error_counter = 0;
    }
    if (rec->remaining_ticks == 0)
        return;
    rec->remaining_ticks -= 1;
    if (rec->remaining_ticks == 0) {
        seq_recovery_reset(rec);
        if (rec->session_id)
            delete_oam_rcvy(rec->session_id);
    }
}

// This thread decrement the @remaining_ticks periodically
// and check do a Sequence Recovery reset if it reach zero.
static void *reset_thread(void *arg)
{
    struct SequenceRecovery *rec = arg;
    struct timespec sleep_until, delta, now;

    //TODO grab mutex

    reset_ticks(rec);

    const time_t tick_ns = NSEC_PER_SEC / FRER_TICKS_PER_SEC;
    delta.tv_sec = tick_ns / NSEC_PER_SEC;
    delta.tv_nsec = tick_ns % NSEC_PER_SEC;
    clock_gettime(CLOCK_REALTIME, &now);
    timespecadd(&now, &delta, &sleep_until);
    while (true) {
        clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &sleep_until, NULL);
        clock_gettime(CLOCK_REALTIME, &now);
        if (timespeccmp(&now, &sleep_until, <)) {
            printf("\tEarly wakeup. continue sleep...\n");
            // Unlikely early wake up, continue with sleeping
            continue;
        }
        decrement_ticks(rec);
        timespecadd(&now, &delta, &sleep_until);
    }
    return rec;
}

struct JsonValue *seqrec_get_state_json(const void *obj)
{
    const struct SequenceRecovery *rec = obj;
    struct JsonValue *js = json_object();
    json_object_insert(js, "passed_packets", json_number((double) rec->passed_packets));
    json_object_insert(js, "discarded_packets", json_number((double) rec->discarded_packets));
    return js;
}

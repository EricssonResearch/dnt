#include "utils.h"
#include "seq_recov.h"
#include "packet.h"

#include <string.h>
#include <stdlib.h>

#include <arpa/inet.h>

struct SequenceRecovery {
    enum SequenceRecoveryAlgorithm algorithm;
    bool use_reset_flag;
    bool use_init_flag;
    bool individual_recovery;
    int history_length;
    unsigned reset_msec;
    unsigned latent_error_paths;

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
    int discard_packets;
    int remaining_ticks;
};

struct SequenceRecovery *new_seq_rec(enum SequenceRecoveryAlgorithm algo, bool use_reset_flag, bool use_init_flag,
        unsigned history_length, unsigned reset_msec, unsigned latent_error_paths)
{
    struct SequenceRecovery *ret = calloc_struct(SequenceRecovery);

    ret->algorithm = algo;
    ret->use_reset_flag = use_reset_flag;
    ret->use_init_flag = use_init_flag;
    ret->history_length = history_length;
    ret->reset_msec = reset_msec;
    ret->latent_error_paths = latent_error_paths;
    ret->history = calloc(history_length, sizeof(char));
    ret->init_history = calloc(history_length, sizeof(char));

    return ret;
}

struct SequenceRecovery *delete_seq_rec(struct SequenceRecovery *rec)
{
    free(rec->history);
    free(rec->init_history);
    free(rec);
    return NULL;
}

static void shift_seq_history(struct SequenceRecovery *rec, unsigned new_zero)
{
    if(rec->history[rec->history_length - 1] == 0)
        rec->lost_packets += 1;
    for(int i = rec->history_length - 1; i != 0; --i)
        rec->history[i] = rec->history[i - 1];
    rec->history[0] = new_zero;
}

static inline int calc_delta(int seq1, int seq2)
{
    int delta = (seq1 - seq2) & (FRER_RCVY_SEQ_SPACE - 1);
    if((delta & (FRER_RCVY_SEQ_SPACE / 2)) != 0)
        delta = delta - FRER_RCVY_SEQ_SPACE;
    return delta;
}

static void reset_ticks(struct SequenceRecovery *rec)
{
        rec->remaining_ticks = ((rec->reset_msec * FRER_TICKS_PER_SEC) + 999) / 1000;
}

static bool recover(struct SequenceRecovery *rec, unsigned packet_seq)
{
    int delta = calc_delta(packet_seq, rec->recv_seq);
    if(rec->take_any) {
        rec->take_any = false;
        rec->history[0] = 1;
        rec->recv_seq = packet_seq;
        rec->passed_packets += 1;
        reset_ticks(rec);
        return true;
    } else if(delta > rec->history_length || delta <= -rec->history_length) {
        rec->rogue_packets += 1;
        rec->discard_packets += 1;

        if(rec->individual_recovery)
            reset_ticks(rec);
    } else if(delta <= 0) {
        if(rec->history[-delta] == 0) {
            rec->history[-delta] = 1;
            rec->out_of_order_packets += 1;
            rec->passed_packets += 1;
            reset_ticks(rec);
            return true;
        } else {
            rec->discard_packets += 1;
            if(rec->individual_recovery)
                reset_ticks(rec);
        }
    } else {
        if(delta != 1)
            rec->out_of_order_packets += 1;

        while((delta -= 1) != 0)
            shift_seq_history(rec, 0);
        shift_seq_history(rec, 1);
        rec->recv_seq = packet_seq;
        rec->passed_packets += 1;
        reset_ticks(rec);
        return true;
    }
    return false;
}

static bool vector_seq_recovery(struct SequenceRecovery *rec, struct Packet *p)
{
    unsigned packet_seq = ntohl(p->sequence) & 0xffff;
    return recover(rec, packet_seq);
}

static bool seamless_seq_recovery(struct SequenceRecovery *rec, struct Packet *p)
{
    int delta = 0;
    // TODO: use proper metadata seq/flags as packet member not rtag format
    unsigned packet_seq = ntohl(p->sequence) & 0xffff;
    if(rec->use_reset_flag) {
        if(rec->use_init_flag) {
            if(p->sequence & FRER_INIT_FLAG) {
                delta = calc_delta(packet_seq, rec->init_recv_seq);
                if(p->sequence & FRER_RESET_FLAG)
                    if((delta > rec->history_length) || (delta <= -rec->history_length * 2))
                        rec->init_take_any = true;
            }

            int high = FRER_RCVY_SEQ_SPACE - 3 * rec->history_length;
            int low = FRER_RCVY_SEQ_SPACE - rec->history_length;
            if((int)packet_seq >= high && (int)packet_seq < low)
                rec->take_any = true;

            return recover(rec, packet_seq);
        }
        if(p->sequence & FRER_RESET_FLAG) {
            delta = calc_delta(packet_seq, rec->recv_seq);
            if((delta > rec->history_length) || (delta <= -rec->history_length * 2))
                rec->take_any = true;
        }
    }
    return recover(rec, packet_seq);
}

bool seq_recovery(struct SequenceRecovery *rec, struct Packet *p)
{
    switch (rec->algorithm) {
        case RCVY_Vector:
            return vector_seq_recovery(rec, p);
        case RCVY_SeamlessVector:
            return seamless_seq_recovery(rec, p);
        // TODO: implement match
        case RCVY_Match:
            return true;
    }
    // TODO:implement all case
    return true;
}

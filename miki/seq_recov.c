
#include "seq_recov.h"
#include "utils.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <arpa/inet.h> /* ntohl() */

struct SequenceRecovery {
    //TODO all the things
    unsigned last_seq;
};

struct SequenceRecovery *new_seq_rec(enum SequenceRecoveryAlgorithm algo,
        bool use_reset_flag, bool use_init_flag,
        unsigned history_length, unsigned reset_msec, unsigned latent_error_paths)
{
    struct SequenceRecovery *ret = calloc_struct(SequenceRecovery);

    (void)algo;
    (void)use_reset_flag;
    (void)use_init_flag;
    (void)history_length;
    (void)reset_msec;
    (void)latent_error_paths;

    return ret;
}

struct SequenceRecovery *delete_seq_rec(struct SequenceRecovery *rec)
{
    free(rec);
    return NULL;
}

static void get_seq_num(void *state, struct Value *value, struct Packet *p)
{
    (void)p;
    uint32_t *seq_num = state;
    memcpy(seq_num, value->value, sizeof(uint32_t));
}

bool seq_recovery(struct SequenceRecovery *rec, value_producer *read_seq, void *producer_state, struct Packet *p)
{
    uint32_t seq_num;
    read_seq(producer_state, get_seq_num, &seq_num, p);
    uint32_t seq = ntohl(seq_num) & 0xffff;
    bool ret = seq > rec->last_seq; //TODO this is the simplest recovery
    printf("seq recovery: 0x%.8x %u %u -> %s\n", seq_num, seq, rec->last_seq, ret ? "new" : "duplicate");
    rec->last_seq = seq;
    return ret;
}

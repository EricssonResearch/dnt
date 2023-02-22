
#include "seq_recov.h"
#include "utils.h"

#include <string.h>
#include <stdlib.h>

struct SequenceRecovery {
    //TODO all the things
};

struct SequenceRecovery *new_seq_rec(bool use_reset_flag, bool use_init_flag,
        unsigned history_length, unsigned reset_msec, unsigned latent_error_paths)
{
    struct SequenceRecovery *ret = calloc_struct(SequenceRecovery);

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

static void get_seq(void *state, struct Value *value, struct Packet *p)
{
    uint32_t *packet_seq = state;
    memcpy(packet_seq, value->value, sizeof(uint32_t));
    (void)p;
}

bool seq_recovery(struct SequenceRecovery *rec, struct Packet *p, value_producer *read_seq, struct HeaderField *seq)
{
    uint32_t packet_seq;
    read_seq(seq, get_seq, &packet_seq, p);

    //TODO check recovery state: we need to import the recover() functions from the old code
    (void)rec;

    return true;
}

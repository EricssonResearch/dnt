
#include "seq_gen.h"
#include "utils.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#include <arpa/inet.h> /* htonl() */

struct SequenceGenerator {
    bool use_reset_flag;
    bool use_init_flag;
    unsigned init_seq;

    unsigned seq;
    bool reset;
    bool init;

};

struct SequenceGenerator *new_seq_gen(bool use_reset_flag, bool use_init_flag, unsigned init_seq)
{
    struct SequenceGenerator *ret = calloc_struct(SequenceGenerator);
    ret->use_reset_flag = use_reset_flag;
    ret->use_init_flag = use_init_flag;
    ret->init_seq = init_seq;

    ret->seq = init_seq;
    ret->reset = use_reset_flag;
    ret->init = use_init_flag;

    return ret;
}

struct SequenceGenerator *delete_seq_gen(struct SequenceGenerator *gen)
{
    free(gen);
    return NULL;
}


static void step_seq(struct SequenceGenerator *gen)
{
    gen->seq++;
    //TODO manage overflow, flags etc.
}

void seq_generator(struct SequenceGenerator *gen, struct Packet *p)
{
    struct SequenceGenerator *gen = state;
    uint32_t seqn = 0;

    //printf("seq gen %u 0x%x\n", gen->seq, gen->seq);
    seqn = htonl(gen->seq); //TODO add gen->flags
    p->sequence = seqn;

    step_seq(gen);
}

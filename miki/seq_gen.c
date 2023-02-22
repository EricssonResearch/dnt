
#include "seq_gen.h"
#include "utils.h"

#include <stdlib.h>
#include <stdint.h>

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

void seq_generator(void *state, value_consumer *consumer, void *consumer_state, struct Packet *p)
{
    struct SequenceGenerator *gen = state;
    uint32_t seqn = 0;
    struct Value val = {&seqn, 0, 32};
    step_seq(gen);

    //TODO seqn = htons(gen->seq) | gen->flags

    consumer(consumer_state, &val, p);
}

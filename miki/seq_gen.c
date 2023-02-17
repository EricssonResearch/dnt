
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

static void step_seq(struct SequenceGenerator *gen)
{
    gen->seq++;
    //TODO manage overflow, flags etc.
}

void seq_generator(struct Packet *p, struct HeaderField *target, field_assign *assign, void *state)
{
    uint32_t seqn = 0;
    struct HeaderValue val = {&seqn, 32};
    struct SequenceGenerator *gen = state;
    step_seq(gen);

    //TODO seq = htons(seq) | flags

    assign(p, target, &val);
}

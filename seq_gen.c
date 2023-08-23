// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "seq_gen.h"
#include "conf_object.h"
#include "seq_recov.h"
#include "packet.h"
#include "utils.h"
#include "json.h"

#include <netinet/in.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#include <arpa/inet.h> /* htonl() */

struct SequenceGenerator {
    bool use_reset_flag;
    bool use_init_flag;
    unsigned init_seq_start;

    unsigned gen_seq_num;
    unsigned init_gen_seq_num;
    bool reset_flag;
    bool use_init_seq_space;

    unsigned resets;
};

struct SequenceGenerator *new_seq_gen(bool use_reset_flag, bool use_init_flag, unsigned init_seq_start)
{
    struct SequenceGenerator *ret = calloc_struct(SequenceGenerator);
    ret->use_reset_flag = use_reset_flag;
    ret->use_init_flag = use_init_flag;
    ret->init_seq_start = init_seq_start;

    ret->gen_seq_num = 0;
    ret->init_gen_seq_num = init_seq_start;
    ret->reset_flag = use_reset_flag;
    ret->use_init_seq_space = use_init_flag;

    return ret;
}

struct SequenceGenerator *delete_seq_gen(struct SequenceGenerator *gen)
{
    free(gen);
    return NULL;
}


static void sequence_generation_reset(struct SequenceGenerator *gen)
{
    gen->reset_flag = gen->use_reset_flag;
    gen->use_init_seq_space = gen->use_init_flag;

    gen->gen_seq_num = 0;
    gen->init_gen_seq_num = gen->init_seq_start;
    gen->resets += 1;
}

int reset_all_seq_generators(const char *key, void *value, void *udata)
{
    (void) key;
    (void) udata;

    struct ConfObject *obj = value;
    if (obj->type == CO_SEQGEN) {
        sequence_generation_reset(obj->object);
    }

    return 1;
}


static unsigned sequence_generation(struct SequenceGenerator *gen)
{
    if (gen->use_init_seq_space) {
        unsigned seq = gen->init_gen_seq_num & 0xffff;
        seq |= (FRER_RESET_FLAG * gen->reset_flag);
        seq |= (FRER_INIT_FLAG * gen->use_init_seq_space);
        if(gen->init_gen_seq_num > (FRER_RCVY_SEQ_SPACE - 1)) {
            gen->gen_seq_num = 1;
            gen->use_init_seq_space = false;
            gen->init_gen_seq_num = gen->init_seq_start;
        } else {
            gen->init_gen_seq_num += 1;
            if (gen->init_gen_seq_num > gen->init_seq_start + FRER_SEQ_GEN_RESET_FLAG_COUNT)
                gen->reset_flag = false;
        }
        return seq;
    }
    //regular seq generator
    unsigned seq = gen->gen_seq_num & 0xffff;
    if (gen->gen_seq_num >= (FRER_RCVY_SEQ_SPACE - 1)) {
        gen->gen_seq_num = 0;
    } else {
        gen->gen_seq_num += 1;
        if (gen->gen_seq_num > FRER_SEQ_GEN_RESET_FLAG_COUNT)
            gen->reset_flag = false;
    }
    seq |= (FRER_RESET_FLAG * gen->reset_flag);
    seq |= (FRER_INIT_FLAG * gen->use_init_seq_space);
    return seq;
}

void seq_generator(struct SequenceGenerator *gen, struct Packet *p)
{
    unsigned new_seq = sequence_generation(gen);
    p->sequence = htonl(new_seq);
}

struct JsonValue *seqgen_get_state_json(const void *obj)
{
    const struct SequenceGenerator *gen = obj;
    struct JsonValue *js = json_object();
    json_object_insert(js, "type", json_string("seqgen"));
    json_object_insert(js, "use_init_flag", gen->use_init_flag ? json_true() : json_false());
    json_object_insert(js, "use_reset_flag", gen->use_reset_flag ? json_true() : json_false());
    return js;
}

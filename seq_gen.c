// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "seq_gen.h"
#include "json.h"
#include "packet.h"
#include "pipeline.h"
#include "seq_recov.h"
#include "utils.h"

#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h> /* htonl() */

#define FRER_SEQ_GEN_RESET_FLAG_COUNT 3

struct SequenceGenerator {
    struct PipelineObject base;

    bool use_reset_flag;
    bool use_init_flag;
    unsigned init_seq_start;

    unsigned gen_seq_num;
    unsigned init_gen_seq_num;
    bool reset_flag;
    bool use_init_seq_space;

    unsigned resets;
};

static struct JsonValue *get_state_json(const struct PipelineObject *obj)
{
    const struct SequenceGenerator *gen = (const struct SequenceGenerator *)obj;
    struct JsonValue *js = json_object();
    json_object_insert(js, "type", json_string("seqgen"));
    json_object_insert(js, "name", json_string(obj->name));
    json_object_insert(js, "use_init_flag", gen->use_init_flag ? json_true() : json_false());
    json_object_insert(js, "use_reset_flag", gen->use_reset_flag ? json_true() : json_false());
    //TODO report more state
    return js;
}

char *seq_gen_sprintf_state_json(struct JsonValue *json, const char *record_sep, const char *line_sep)
{
    (void)line_sep;
    struct JsonValue *use_init_flag = json_object_get_bool(json, "use_init_flag");
    struct JsonValue *use_reset_flag = json_object_get_bool(json, "use_reset_flag");

    if (use_init_flag && use_reset_flag) {
        return strdup_printf("use_init_flag %s%suse_reset_flag %s",
                (use_init_flag->type == JSON_TRUE) ? "true" : "false", record_sep,
                (use_reset_flag->type == JSON_TRUE) ? "true" : "false");
    } else {
        return strdup("<invalid seq_gen state>");
    }
}

struct PipelineObject *new_seq_gen(const char *name, bool use_reset_flag, bool use_init_flag, unsigned init_seq_start)
{
    struct SequenceGenerator *ret = calloc_struct(SequenceGenerator);
    ret->base.type = PO_SEQGEN;
    ret->base.name = strdup(name);
    ret->base.get_state = get_state_json;
    ret->base.process_packet = seq_generator;

    ret->use_reset_flag = use_reset_flag;
    ret->use_init_flag = use_init_flag;
    ret->init_seq_start = init_seq_start;

    ret->gen_seq_num = 0;
    ret->init_gen_seq_num = init_seq_start;
    ret->reset_flag = use_reset_flag;
    ret->use_init_seq_space = use_init_flag;

    return (struct PipelineObject *)ret;
}

struct PipelineObject *delete_seq_gen(struct PipelineObject *gen)
{
    //TODO throw if gen->type is not PO_SEQGEN
    free(gen->name);
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

    struct PipelineObject *obj = (struct PipelineObject *)value;
    if (obj->type == PO_SEQGEN) {
        struct SequenceGenerator *g = (struct SequenceGenerator *)obj;
        sequence_generation_reset(g);
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

enum ActionResult seq_generator(struct PipelineObject *gen, struct PipelineIterator *pi)
{
    struct SequenceGenerator *g = (struct SequenceGenerator *)gen;
    unsigned new_seq = sequence_generation(g);
    pi->packet->sequence = htonl(new_seq);
    return ACR_CONTINUE;
}



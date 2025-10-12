// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.

#define OBJECT_INTERNAL

#include "seq_gen.h"
#include "action.h"
#include "json.h"
#include "log.h"
#include "notification.h"
#include "packet.h"
#include "pipeline.h"
#include "seq_recov.h"
#include "utils.h"

#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h> /* htonl() */

#define FRER_SEQ_GEN_RESET_FLAG_COUNT 3

LOGGING_MODULE(PACKETTRACE, WARNING);

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

static struct JsonValue *sgen_get_state_json(const struct PipelineObject *obj)
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

static NotificationLevel seq_gen_notification_pull_fn(void *self, struct JsonValue **msg)
{
    struct PipelineObject *rep = (struct PipelineObject *)self;
    struct JsonValue *js = sgen_get_state_json(rep);
    *msg = js;
    return NOTIF_PULL;
}

static void sgen_print_info(const struct PipelineObject *self, FILE *cmd_w)
{
    const struct SequenceGenerator *gen = (const struct SequenceGenerator *)self;

    fprintf(cmd_w, "    use reset flag %s init flag %s init start %u\n"
                   "    seq %u init seq %u reset %s init %s resets %u\n",
                   gen->use_reset_flag ? "yes" : "no", gen->use_init_flag ? "yes" : "no", gen->init_seq_start,
                   gen->gen_seq_num, gen->init_gen_seq_num,
                   gen->reset_flag ? "\033[31m]YES\033[0m" : "no",
                   gen->use_init_seq_space ? "\033[35m]YES\033[0m" : "no",
                   gen->resets);
}

static void sequence_generation_reset(struct SequenceGenerator *gen)
{
    gen->reset_flag = gen->use_reset_flag;
    gen->use_init_seq_space = gen->use_init_flag;

    gen->gen_seq_num = 0;
    gen->init_gen_seq_num = gen->init_seq_start;
    gen->resets += 1;
}

int reset_seq_generator(struct PipelineObject *obj, void *userdata)
{
    (void) userdata;

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

static enum ActionResult seq_generator(struct PipelineObject *gen, struct PipelineIterator *pi)
{
    struct SequenceGenerator *g = (struct SequenceGenerator *)gen;
    unsigned new_seq = sequence_generation(g);
    PACKET_LOGCAT(pi->packet, "(%u) ", new_seq & 0xffff);
    pi->packet->sequence = htonl(new_seq);
    return ACR_CONTINUE;
}

struct PipelineObject *new_seq_gen(const char *name, bool use_reset_flag, bool use_init_flag, unsigned init_seq_start)
{
    struct SequenceGenerator *ret = calloc_struct(SequenceGenerator);
    ret->base.type = PO_SEQGEN;
    ret->base.name = strdup(name);
    ret->base.get_state = sgen_get_state_json;
    ret->base.process_packet = seq_generator;
    ret->base.print_info = sgen_print_info;
    ret->base.reference_count = 1;

    ret->use_reset_flag = use_reset_flag;
    ret->use_init_flag = use_init_flag;
    ret->init_seq_start = init_seq_start;

    ret->gen_seq_num = 0;
    ret->init_gen_seq_num = init_seq_start;
    ret->reset_flag = use_reset_flag;
    ret->use_init_seq_space = use_init_flag;

    notification_register_source(name, seq_gen_notification_pull_fn, ret, 2000);

    return (struct PipelineObject *)ret;
}

struct PipelineObject *delete_seq_gen(struct PipelineObject *gen)
{
    notification_register_source(gen->name, NULL, NULL, 2000);
    //TODO throw if gen->type is not PO_SEQGEN
    free(gen->name);
    free(gen);
    return NULL;
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

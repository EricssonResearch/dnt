
#include "testing.h"

#include "notification.h"
#include "pipeline.h"
#include "seq_gen.h"
#include "seq_recov.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>

TEST_INIT("Sequence Generator");

// XXX stubs for stuff that we transitively depend on but don't need
void iface_add_sender(struct Interface *i);
void iface_add_sender(struct Interface *i) { (void)i; }
void iface_del_sender(struct Interface *i);
void iface_del_sender(struct Interface *i) { (void)i; }
struct Action *delete_action(struct Action *a) { (void)a; return NULL; }
struct Interface *action_send_get_iface(struct Action *a) { (void)a; return NULL; }
const char *action_name_from_type(enum ActionType type) { (void)type; return NULL; }
struct PipelineList *action_repl_get_piplinelist(struct Action *a) { (void)a; return NULL; }
bool notification_push_event(const char *source, NotificationLevel level, struct JsonValue *message)
    { (void)source; (void)level; json_delete(message); return false; }
bool notification_register_source(const char *name, notification_pull_fn *callback, void *self, unsigned period_ms)
    { (void)name; (void)callback; (void)self; (void)period_ms; return true; }
struct PipelineObject *delete_pof(struct PipelineObject *pof);
struct PipelineObject *delete_pof(struct PipelineObject *pof) { (void)pof; return NULL; }
char *pof_sprintf_state_json(struct JsonValue *json, const char *record_sep, const char *line_sep);
char *pof_sprintf_state_json(struct JsonValue *json, const char *record_sep, const char *line_sep)
    { (void)json; (void)record_sep; (void)line_sep; return NULL; }
struct PipelineObject *delete_replicate(struct PipelineObject *rep);
struct PipelineObject *delete_replicate(struct PipelineObject *rep) { (void)rep; return NULL; }
char *repl_sprintf_state_json(struct JsonValue *json, const char *record_sep, const char *line_sep);
char *repl_sprintf_state_json(struct JsonValue *json, const char *record_sep, const char *line_sep)
    { (void)json; (void)record_sep; (void)line_sep; return NULL; }
struct PipelineObject *delete_seq_rec(struct PipelineObject *rec);
struct PipelineObject *delete_seq_rec(struct PipelineObject *rec) { (void)rec; return NULL; }
char *seq_rec_sprintf_state_json(struct JsonValue *json, const char *record_sep, const char *line_sep);
char *seq_rec_sprintf_state_json(struct JsonValue *json, const char *record_sep, const char *line_sep)
    { (void)json; (void)record_sep; (void)line_sep; return NULL; }
// XXX end stubs

static struct Pipeline *new_pipeline(const char *name)
{
    struct Pipeline *ret = calloc_struct(Pipeline);
    ret->name = strdup(name);
    return ret;
}

static void test_gen(void)
{
    struct PipelineObject *gen = new_seq_gen("test gen", false, false, 2000);
    OK_FATAL(gen, "have object");

    struct Packet *p = new_packet(NULL);
    OK_FATAL(p, "have packet");
    struct Pipeline *pl = new_pipeline("test");
    OK_FATAL(pl, "have pipeline");
    struct PipelineIterator *pi = new_pipe_iterator(pl, p);
    OK_FATAL(pi, "have iterator");

    unsigned short counter = 0;
    for (unsigned i=0; i<1000000; i++) {
        OK(gen->process_packet(gen, pi) == ACR_CONTINUE, "always lets through");
        unsigned seq = ntohl(p->sequence); // this should be a 16 bit counter stored in 32 bits
        OK(seq == counter, "%u seq %u counter %u", i, seq, counter);
        counter++;
    }

    pipeline_object_unref(gen);
    free(pi);
    pipeline_unref(pl);
    OK(delete_packet(p) == NULL, "delete packet");
}

static void test_reset(void)
{
    struct PipelineObject *gen = new_seq_gen("test gen", false, false, 2000);
    OK_FATAL(gen, "have object");
    struct PipelineObject *genR = new_seq_gen("test gen", true, false, 2000);
    OK_FATAL(genR, "have object");

    struct Packet *p = new_packet(NULL);
    OK_FATAL(p, "have packet");
    struct Pipeline *pl = new_pipeline("test");
    OK_FATAL(pl, "have pipeline");
    struct PipelineIterator *pi = new_pipe_iterator(pl, p);
    OK_FATAL(pi, "have iterator");

    unsigned short counter = 0;
    unsigned short rcounter = 0;
    for (unsigned i=0; i<1000000; i++) {
        OK(gen->process_packet(gen, pi) == ACR_CONTINUE, "always lets through");
        unsigned seq = ntohl(p->sequence); // this should be a 16 bit counter stored in 32 bits
        OK(seq == counter, "%u seq 0x%x counter 0x%x", i, seq, counter);

        OK(genR->process_packet(genR, pi) == ACR_CONTINUE, "always lets through");
        seq = ntohl(p->sequence); // this should be a 16 bit counter stored in 32 bits
        unsigned rcounter_flag = rcounter;
        if (rcounter < FRER_SEQ_GEN_RESET_FLAG_COUNT)
            rcounter_flag |= FRER_RESET_FLAG;
        OK(seq == rcounter_flag, "%u seq 0x%x counter 0x%x", i, seq, rcounter_flag);

        if (counter == 10000) {
            reset_seq_generator(gen, NULL);
            counter = 0;
        } else
            counter++;

        if (rcounter == 11000) {
            reset_seq_generator(genR, NULL);
            rcounter = 0;
        } else
            rcounter++;
    }

    pipeline_object_unref(gen);
    pipeline_object_unref(genR);
    free(pi);
    pipeline_unref(pl);
    OK(delete_packet(p) == NULL, "delete packet");
}

static void test_init(void)
{
    unsigned init = 40000;
    struct PipelineObject *gen = new_seq_gen("test gen", true, true, init);
    OK_FATAL(gen, "have object");

    struct Packet *p = new_packet(NULL);
    OK_FATAL(p, "have packet");
    struct Pipeline *pl = new_pipeline("test");
    OK_FATAL(pl, "have pipeline");
    struct PipelineIterator *pi = new_pipe_iterator(pl, p);
    OK_FATAL(pi, "have iterator");

    unsigned short counter = init;
    bool in_init = true;
    for (unsigned i=0; i<1000000; i++) {
        OK(gen->process_packet(gen, pi) == ACR_CONTINUE, "always lets through");
        unsigned seq = ntohl(p->sequence); // this should be a 16 bit counter stored in 32 bits

        unsigned counter_flag = counter;
        if (in_init) {
            counter_flag |= FRER_INIT_FLAG;
            if (counter - init < FRER_SEQ_GEN_RESET_FLAG_COUNT)
                counter_flag |= FRER_RESET_FLAG;

            OK(seq == counter_flag, "%u seq 0x%x counter 0x%x", i, seq, counter_flag);
            counter++;
            if (counter == 0)
                in_init = false;
        } else {
            OK(seq == counter_flag, "%u seq 0x%x counter 0x%x", i, seq, counter_flag);
            counter++;
        }

        if (counter == 10000) {
            reset_seq_generator(gen, NULL);
            counter = init;
            in_init = true;
        }
    }

    pipeline_object_unref(gen);
    free(pi);
    pipeline_unref(pl);
    OK(delete_packet(p) == NULL, "delete packet");
}


//TODO multithread: reset from another thread
//      it's tricky to verify the sequence number for this....

TEST_CASES = {
    {"generator", test_gen},
    {"reset", test_reset},
    {"init", test_init},
    {NULL, NULL}
};

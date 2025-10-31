
#include "testing.h"

#include "action.h"
#include "json.h"
#include "notification.h"
#include "packet.h"
#include "pipeline.h"
#include "seq_recov.h"
#include "thread_utils.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>

#include <pthread.h>

#include <arpa/inet.h>

TEST_INIT("Sequence Recovery: Match");

// XXX stubs for stuff that we transitively depend on but don't need
void oam_cli_alert(const char *s, ...);
void oam_cli_alert(const char *s, ...) { (void)s; }
void iface_ref(void);
void iface_ref(void) {}
void iface_unref(void);
void iface_unref(void) {}
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
struct PipelineObject *delete_seq_gen(struct PipelineObject *gen);
struct PipelineObject *delete_seq_gen(struct PipelineObject *gen) { (void)gen; return NULL; }
char *seq_gen_sprintf_state_json(struct JsonValue *json, const char *record_sep, const char *line_sep);
char *seq_gen_sprintf_state_json(struct JsonValue *json, const char *record_sep, const char *line_sep)
    { (void)json; (void)record_sep; (void)line_sep; return NULL; }
void oam_automip_start_mask_session(const char *mip_name);
void oam_automip_start_mask_session(const char *mip_name) { (void)mip_name; }
void oam_automip_stop_mask_session(const char *mip_name);
void oam_automip_stop_mask_session(const char *mip_name) { (void)mip_name; }
void oam_pipeline_deleted(struct Pipeline *pipe);
void oam_pipeline_deleted(struct Pipeline *pipe) { (void)pipe; }
// XXX end stubs

static const unsigned history_length = 64; // must be 2^n
static const unsigned reset_ms = 30;

// we don't want to use assemble_actions() in this test
// and we don't care about the contents
static struct Pipeline *new_pipeline(const char *name)
{
    struct Pipeline *ret = calloc_struct(Pipeline);
    ret->name = strdup(name);
    return ret;
}

static void test_duplicates(void)
{
    // note: only @reset_ms has effect
    struct RecoveryDiagnosticConf diag = {};
    struct PipelineObject *rec = new_seq_rec("match", RCVY_Match, false, false, history_length, reset_ms, &diag);
    OK_FATAL(rec, "have object");

    unsigned start = 200;
    struct Packet *p = new_packet(NULL);
    OK_FATAL(p, "have packet");
    struct Pipeline *pl = new_pipeline("test");
    OK_FATAL(pl, "have pipeline");
    struct PipelineIterator *pi = new_pipe_iterator(pl, p);
    OK_FATAL(pi, "have iterator");

    p->sequence = htonl(start);
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "in TakeAny");
    OK(rec->process_packet(rec, pi) == ACR_DONE, "duplicate");
    p->sequence = htonl(start+1);
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "not duplicate");
    OK(rec->process_packet(rec, pi) == ACR_DONE, "duplicate");
    p->sequence = htonl(start);
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "not duplicate");
    OK(rec->process_packet(rec, pi) == ACR_DONE, "duplicate");
    p->sequence = htonl(start-1);
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "not duplicate");
    OK(rec->process_packet(rec, pi) == ACR_DONE, "duplicate");
    p->sequence = htonl(start);
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "not duplicate");
    OK(rec->process_packet(rec, pi) == ACR_DONE, "duplicate");

    usleep(1000*(reset_ms+100)); //TODO the needed oversleep depends on cpu speed :(
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "in TakeAny again");
    OK(rec->process_packet(rec, pi) == ACR_DONE, "duplicate");

    // test the seq overflow point
    p->sequence = htonl(0xffff);
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "not duplicate");
    OK(rec->process_packet(rec, pi) == ACR_DONE, "duplicate");
    p->sequence = htonl(0);
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "not duplicate");
    OK(rec->process_packet(rec, pi) == ACR_DONE, "duplicate");
    p->sequence = htonl(0xffff);
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "not duplicate");
    OK(rec->process_packet(rec, pi) == ACR_DONE, "duplicate");

    pipeline_object_unref(rec);
    free(pi);
    pipeline_unref(pl);
    OK(delete_packet(p) == NULL, "delete packet");
}

static void test_single(void)
{
    struct RecoveryDiagnosticConf diag = {};
    struct PipelineObject *rec = new_seq_rec("match", RCVY_Match, false, false, history_length, reset_ms, &diag);
    OK_FATAL(rec, "have object");

    srand(2020); // this seed looks nice

    const unsigned start = 2023;
    const unsigned interval = 100;
    const unsigned iterations = 10000;
    unsigned last = start-1;
    struct Packet *p = new_packet(NULL);
    OK_FATAL(p, "have packet");
    struct Pipeline *pl = new_pipeline("test");
    OK_FATAL(pl, "have pipeline");
    struct PipelineIterator *pi = new_pipe_iterator(pl, p);
    OK_FATAL(pi, "have iterator");
    for (unsigned i=0; i<iterations; i++) {
        unsigned seq = start + rand() % interval;
        p->sequence = htonl(seq);
        enum ActionResult result = rec->process_packet(rec, pi);
        enum ActionResult good = last != seq ? ACR_CONTINUE : ACR_DONE;
        OK(result == good, "match %u last %u seq %u result %d", i, last, seq, result);
        last = seq;
    }

    pipeline_object_unref(rec);
    free(pi);
    pipeline_unref(pl);
    OK(delete_packet(p) == NULL, "delete packet");
}

#define ITERATIONS 300
static pthread_spinlock_t spinlock;
static unsigned seq = 0;
static unsigned results[ITERATIONS*2+1] = {};

static void *multi_thread(void *arg)
{
    // note: testing.h is not thread-safe, we can't use OK() here

    //pthread_t tid = pthread_self();
    struct PipelineObject *rec = (struct PipelineObject *)arg;

    struct Packet *p = new_packet(NULL);
    struct Pipeline *pl = new_pipeline("test");
    struct PipelineIterator *pi = new_pipe_iterator(pl, p);

    for (unsigned i=0; i<ITERATIONS; i++) {
        pthread_spin_lock(&spinlock);

        p->sequence = htonl(seq);
        enum ActionResult result = rec->process_packet(rec, pi);
        if (result == ACR_CONTINUE)
            __atomic_add_fetch(&results[seq], 1, __ATOMIC_RELAXED);

        //usleep(10*1000);
        volatile int k = 0;
        for (unsigned a=0; a<10000000;a++) {
            k += a;
        }
        seq++;

        pthread_spin_unlock(&spinlock);

        p->sequence = htonl(seq);
        result = rec->process_packet(rec, pi);
        if (result == ACR_CONTINUE)
            __atomic_add_fetch(&results[seq], 1, __ATOMIC_RELAXED);

        //printf("\n%lu %u", tid, i);
    }

    free(pi);
    pipeline_unref(pl);
    delete_packet(p);
    return NULL;
}

static void test_multi(void)
{
    OK_FATAL(pthread_spin_init(&spinlock, 0) == 0, "create spinlock");

    struct RecoveryDiagnosticConf diag = {};
    struct PipelineObject *rec = new_seq_rec("match", RCVY_Match, false, false, history_length, 1000, &diag);
    OK_FATAL(rec, "have object");

    struct Thread *t[2];
    for (unsigned i=0; i<2; i++) {
        t[i] = thread_launch(multi_thread, rec, "rcvy %u", i);
        //printf("\nlaunched %p", t[i]);
    }

    for (unsigned i=0; i<2; i++) {
        thread_join(t[i]);
    }

#define VERIFY_COUNTER(name, expected)                                              \
    do {                                                                            \
        struct JsonValue *cnt = json_object_get_number(rec_state, #name);           \
        OK_FATAL(cnt != NULL, "recovery state is missing " #name);                  \
        OK(cnt->v.number == expected, "recovery " #name " %.0f", cnt->v.number);    \
    } while (0)

    struct JsonValue *rec_state = rec->get_state(rec);
    OK_FATAL(rec_state, "have state");
    VERIFY_COUNTER(passed_packets, ITERATIONS*2 + 1);
    VERIFY_COUNTER(discarded_packets, ITERATIONS*2 - 1);
    VERIFY_COUNTER(out_of_order_packets, 0);
    VERIFY_COUNTER(recovery_seq_num, ITERATIONS*2);
    VERIFY_COUNTER(seq_recovery_resets, 0);
    json_delete(rec_state);

#undef VERIFY_COUNTER

    pipeline_object_unref(rec);
    pthread_spin_destroy(&spinlock);

    for (unsigned i=0; i<ARRAY_SIZE(results); i++) {
        OK(results[i] == 1, "seq %u is %u", i, results[i]);
    }
}

TEST_CASES = {
    {"duplicates", test_duplicates},
    {"stress single-thread", test_single},
    {"stress multi-thread", test_multi},
    {NULL, NULL}
};


#include "testing.h"

#include "seq_recov.h"
#include "action.h"
#include "packet.h"
#include "pipeline.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>

#include <unistd.h>
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
    struct PipelineObject *rec = new_seq_rec("match", RCVY_Match, false, false, history_length, reset_ms, NULL);
    OK_FATAL(rec, "have object");

    unsigned start = 200;
    struct Packet *p = new_packet(NULL);
    OK(p, "have packet");
    struct Pipeline *pl = new_pipeline("test");
    OK(pl, "have pipeline");
    struct PipelineIterator *pi = new_pipe_iterator(pl, p);
    OK(pi, "have iterator");

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

    usleep(1000*(reset_ms+30)); //TODO the needed oversleep depends on cpu speed :(
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

    OK(delete_seq_rec(rec) == NULL, "delete object");
    free(pi);
    pipeline_unref(pl);
    OK(delete_packet(p) == NULL, "delete packet");
}

static void test_single(void)
{
    struct PipelineObject *rec = new_seq_rec("match", RCVY_Match, false, false, history_length, reset_ms, NULL);
    OK_FATAL(rec, "have object");

    srand(2020); // this seed looks nice

    const unsigned start = 2023;
    const unsigned interval = 100;
    const unsigned iterations = 10000;
    unsigned last = start-1;
    struct Packet *p = new_packet(NULL);
    OK(p, "have packet");
    struct Pipeline *pl = new_pipeline("test");
    OK(pl, "have pipeline");
    struct PipelineIterator *pi = new_pipe_iterator(pl, p);
    OK(pi, "have iterator");
    for (unsigned i=0; i<iterations; i++) {
        unsigned seq = start + rand() % interval;
        p->sequence = htonl(seq);
        enum ActionResult result = rec->process_packet(rec, pi);
        enum ActionResult good = last != seq ? ACR_CONTINUE : ACR_DONE;
        OK(result == good, "match %u last %u seq %u result %d", i, last, seq, result);
        last = seq;
    }

    OK(delete_seq_rec(rec) == NULL, "delete object");
    free(pi);
    pipeline_unref(pl);
    OK(delete_packet(p) == NULL, "delete packet");
}

static void test_multi(void)
{
    SKIP("first implement the locking");

    // note: match recovery is intended for slow streams
}

TEST_CASES = {
    {"Duplicates", test_duplicates},
    {"Stress Single-Thread", test_single},
    {"Stress Multi-Thread", test_multi},
    {NULL, NULL}
};

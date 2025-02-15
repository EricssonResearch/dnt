
#include "testing.h"

#include "action.h"
#include "notification.h"
#include "packet.h"
#include "pipeline.h"
#include "seq_recov.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>

TEST_INIT("Sequence Recovery: Vector");

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
    { (void)source; (void)level; (void)message; return false; }
bool notification_register_source(const char *name, notification_pull_fn *callback, void *self, unsigned period_ms)
    { (void)name; (void)callback; (void)self; (void)period_ms; return true; }
// XXX end stubs

enum { history_length = 64 }; // must be 2^n
static const unsigned reset_ms = 30;

// we don't want to use assemble_actions() in this test
// and we don't care about the contents
static struct Pipeline *new_pipeline(const char *name)
{
    struct Pipeline *ret = calloc_struct(Pipeline);
    ret->name = strdup(name);
    return ret;
}

static void test_window(void)
{
    struct PipelineObject *rec = new_seq_rec("vector", RCVY_Vector, false, false, history_length, reset_ms, NULL);
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
    p->sequence = htonl(start+1);
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "not duplicate and move window");
    p->sequence = htonl(start-1);
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "not duplicate");
    p->sequence = htonl(start);
    OK(rec->process_packet(rec, pi) == ACR_DONE, "duplicate");
    p->sequence = htonl(start+1);
    OK(rec->process_packet(rec, pi) == ACR_DONE, "duplicate");
    p->sequence = htonl(start-1);
    OK(rec->process_packet(rec, pi) == ACR_DONE, "duplicate");

    // window center is at start+1 now

    p->sequence = htonl(start+1 - history_length);
    OK(rec->process_packet(rec, pi) == ACR_DONE, "window edge outside");
    p->sequence = htonl(start+1 - history_length+1);
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "window edge inside");

    p->sequence = htonl(start+1 + history_length+1);
    OK(rec->process_packet(rec, pi) == ACR_DONE, "window edge outside");
    p->sequence = htonl(start+1 + history_length);
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "window edge inside and move window");

    // window center is at start+history_length now

    p->sequence = htonl(start+history_length - history_length);
    OK(rec->process_packet(rec, pi) == ACR_DONE, "window edge outside");
    p->sequence = htonl(start+history_length - history_length+1); // we already had start+1
    OK(rec->process_packet(rec, pi) == ACR_DONE, "duplicate");
    p->sequence = htonl(start+history_length - history_length+2);
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "window edge inside");

    // move window center to start+history_length*1.5
    p->sequence = htonl(start+history_length + history_length/2);
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "not duplicate and move window");
    p->sequence = htonl(start+history_length*1.5 - history_length);
    OK(rec->process_packet(rec, pi) == ACR_DONE, "window edge outside");
    p->sequence = htonl(start+history_length*1.5 - history_length+1);
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "window edge inside");

    unsigned newstart = 2000;
    p->sequence = htonl(newstart);
    OK(rec->process_packet(rec, pi) == ACR_DONE, "outside");
    usleep(1000*(reset_ms+100)); //TODO the needed oversleep depends on cpu speed :(
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "in TakeAny again");

    // sequence wrap-around
    usleep(1000*(reset_ms+30));
    newstart = 65535;
    p->sequence = htonl(newstart);
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "in TakeAny again");
    p->sequence = htonl(20);
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "in window");
    p->sequence = htonl(65520);
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "in window");

    OK(delete_seq_rec(rec) == NULL, "delete object");
    free(pi);
    pipeline_unref(pl);
    OK(delete_packet(p) == NULL, "delete packet");
}

static void test_single(void)
{
    struct PipelineObject *rec = new_seq_rec("vector", RCVY_Vector, false, false, history_length, reset_ms, NULL);
    OK_FATAL(rec, "have object");

    srand(2020); // this seed looks nice

    struct Packet *p = new_packet(NULL);
    OK_FATAL(p, "have packet");
    struct Pipeline *pl = new_pipeline("test");
    OK_FATAL(pl, "have pipeline");
    struct PipelineIterator *pi = new_pipe_iterator(pl, p);
    OK_FATAL(pi, "have iterator");

    enum {
        start = history_length * 3,
        length = history_length * 10 };
    int results[start + length + history_length*2];
    for (unsigned i=0; i<ARRAY_SIZE(results); i++)
        results[i] = 0;

    int indices[history_length*4];
    for (unsigned i=0; i<ARRAY_SIZE(indices); i++) {
        indices[i] = i - history_length*2;
    }

    for (unsigned j=0; j<length; j++) {
        // scramble the indices
        for (unsigned i=0; i<ARRAY_SIZE(indices); i++) {
            unsigned r = rand() % ARRAY_SIZE(indices);
            int d = indices[i];
            indices[i] = indices[r];
            indices[r] = d;
        }
        /*for (unsigned i=0; i<ARRAY_SIZE(indices); i++) {
          printf(" %d", indices[i]);
        }
        printf("\n");*/

        for (unsigned i=0; i<ARRAY_SIZE(indices); i++) {
            unsigned d = start + j + indices[i];
            p->sequence = htonl(d);
            results[d] += rec->process_packet(rec, pi) == ACR_CONTINUE;
        }
    }
    for (unsigned i=0; i<ARRAY_SIZE(results); i++)
        OK(results[i] < 2, "duplicate %u", i);

    OK(delete_seq_rec(rec) == NULL, "delete object");
    free(pi);
    pipeline_unref(pl);
    OK(delete_packet(p) == NULL, "delete packet");
}

static void test_multi(void)
{
    SKIP("first implement the locking");

    //TODO same as test_single but from two threads simultaneously
    //TODO they should start at the same time (main thread releases a semaphore)
    //TODO testing.h cannot handle multithreading, we must not do OK() in the threads

}

TEST_CASES = {
    {"window", test_window},
    {"stress single-thread", test_single},
    {"stress multi-thread", test_multi},
    {NULL, NULL}
};

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

TEST_INIT("Sequence Recovery: Seamless");

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

// in this test we assume that vector recovery works fine, and only test the reset&init mechanism

#define CHECK_STATE(_js, _key, _expected) \
    do { \
        struct JsonValue *value = json_object_get_number(_js, #_key);\
        OK_FATAL(value, "have " #_key); \
        OK(value->v.number == _expected, #_key " %.0f", value->v.number);\
    } while (0)

static void test_no_flag(void)
{
    struct RecoveryDiagnosticConf diag = {};
    struct PipelineObject *rec = new_seq_rec("seamless", RCVY_SeamlessVector, false, false, history_length, reset_ms, &diag);
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

    p->sequence = htonl(0);
    OK(rec->process_packet(rec, pi) == ACR_DONE, "0 is well outside the window");
    p->sequence = htonl(0 | FRER_RESET_FLAG);
    OK(rec->process_packet(rec, pi) == ACR_DONE, "reset should be ignored");
    p->sequence = htonl(0 | FRER_INIT_FLAG);
    OK(rec->process_packet(rec, pi) == ACR_DONE, "init should be ignored");
    p->sequence = htonl(0 | FRER_INIT_FLAG | FRER_RESET_FLAG);
    OK(rec->process_packet(rec, pi) == ACR_DONE, "both flags should be ignored");

    pipeline_object_unref(rec);
    free(pi);
    pipeline_unref(pl);
    OK(delete_packet(p) == NULL, "delete packet");
}

static void test_reset_flag(void)
{
    struct RecoveryDiagnosticConf diag = {};
    struct PipelineObject *rec = new_seq_rec("seamless", RCVY_SeamlessVector, true, false, history_length, reset_ms, &diag);
    OK_FATAL(rec, "have object");

    unsigned start = 200;
    struct Packet *p = new_packet(NULL);
    OK_FATAL(p, "have packet");
    struct Pipeline *pl = new_pipeline("test");
    OK_FATAL(pl, "have pipeline");
    struct PipelineIterator *pi = new_pipe_iterator(pl, p);
    OK_FATAL(pi, "have iterator");

    unsigned short s;

    p->sequence = htonl(start);
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "in TakeAny");
    p->sequence = htonl(start+1);
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "not duplicate and move window");
    p->sequence = htonl(start-1);
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "not duplicate"); // out of order 1
    p->sequence = htonl(start);
    OK(rec->process_packet(rec, pi) == ACR_DONE, "duplicate");
    p->sequence = htonl(start+1);
    OK(rec->process_packet(rec, pi) == ACR_DONE, "duplicate");
    p->sequence = htonl(start-1);
    OK(rec->process_packet(rec, pi) == ACR_DONE, "duplicate");

    // window center is at start+1 now

    // reset to 0
    p->sequence = htonl(0 | FRER_RESET_FLAG);
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "reset"); // not out of order!
    OK(rec->process_packet(rec, pi) == ACR_DONE, "second reset should be ignored");
    p->sequence = htonl(start);
    OK(rec->process_packet(rec, pi) == ACR_DONE, "out of window after reset"); // rogue 1
    s = 0 - history_length;
    p->sequence = htonl(s);
    OK(rec->process_packet(rec, pi) == ACR_DONE, "window edge outside"); // rogue 2
    s = 0 - history_length+1;
    p->sequence = htonl(s);
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "window edge inside"); // out of order 2
    p->sequence = htonl(0 + history_length+1);
    OK(rec->process_packet(rec, pi) == ACR_DONE, "window edge outside"); // rogue 3
    p->sequence = htonl(0 + history_length);
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "window edge inside"); // out of order 3

    // window center is at 0+history_length now

    struct JsonValue *state = rec->get_state(rec);
    CHECK_STATE(state, passed_packets, 6);
    CHECK_STATE(state, discarded_packets, 7);
    CHECK_STATE(state, out_of_order_packets, 3);
    CHECK_STATE(state, rogue_packets, 3);
    CHECK_STATE(state, seq_recovery_resets, 0); // reset only means timeout-->takeany
    json_delete(state);

    // Reset Ignore Range
    p->sequence = htonl(2 | FRER_RESET_FLAG);
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "in the window, reset should be ignored");
    state = rec->get_state(rec);
    CHECK_STATE(state, passed_packets, 7);
    CHECK_STATE(state, out_of_order_packets, 4);
    json_delete(state);

    // I'm not sure this should be allowed, but the spec doesn't forbid it
    // note: with FRER_INIT_FLAG also set we can reset to anything and it's okay
    p->sequence = htonl(500 | FRER_RESET_FLAG);
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "reset to 500");
    p->sequence = htonl(501);
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "after reset");

    pipeline_object_unref(rec);
    free(pi);
    pipeline_unref(pl);
    OK(delete_packet(p) == NULL, "delete packet");
}

static void test_init_flag(void)
{
    struct RecoveryDiagnosticConf diag = {};
    struct PipelineObject *rec = new_seq_rec("seamless", RCVY_SeamlessVector, true, true, history_length, reset_ms, &diag);
    OK_FATAL(rec, "have object");

    unsigned start = 200;
    unsigned startI = 700;

    struct Packet *p = new_packet(NULL);
    OK_FATAL(p, "have packet");
    struct Pipeline *pl = new_pipeline("test");
    OK_FATAL(pl, "have pipeline");
    struct PipelineIterator *pi = new_pipe_iterator(pl, p);
    OK_FATAL(pi, "have iterator");

    // here we test that there are two independent vector recovery spaces
    //  one with the FRER_INIT_FLAG and one without
    //  we don't even need FRER_RESET_FLAG :)

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

    p->sequence = htonl((startI) | FRER_INIT_FLAG);
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "in TakeAny");
    p->sequence = htonl((startI+1) | FRER_INIT_FLAG);
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "not duplicate and move window");
    p->sequence = htonl((startI-1) | FRER_INIT_FLAG);
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "not duplicate");
    p->sequence = htonl((startI) | FRER_INIT_FLAG);
    OK(rec->process_packet(rec, pi) == ACR_DONE, "duplicate");
    p->sequence = htonl((startI+1) | FRER_INIT_FLAG);
    OK(rec->process_packet(rec, pi) == ACR_DONE, "duplicate");
    p->sequence = htonl((startI-1) | FRER_INIT_FLAG);
    OK(rec->process_packet(rec, pi) == ACR_DONE, "duplicate");
    // init window center is at startI+1

    // check init window
    p->sequence = htonl((startI+1 - history_length) | FRER_INIT_FLAG);
    OK(rec->process_packet(rec, pi) == ACR_DONE, "window edge outside");
    p->sequence = htonl((startI+1 - history_length+1) | FRER_INIT_FLAG);
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "window edge inside");

    p->sequence = htonl((startI+1 + history_length+1) | FRER_INIT_FLAG);
    OK(rec->process_packet(rec, pi) == ACR_DONE, "window edge outside");
    p->sequence = htonl((startI+1 + history_length) | FRER_INIT_FLAG);
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "window edge inside and move window");

    // check non-flagged window
    // window center is at start+1
    p->sequence = htonl(start+1 - history_length);
    OK(rec->process_packet(rec, pi) == ACR_DONE, "window edge outside");
    p->sequence = htonl(start+1 - history_length+1);
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "window edge inside");

    p->sequence = htonl(start+1 + history_length+1);
    OK(rec->process_packet(rec, pi) == ACR_DONE, "window edge outside");
    p->sequence = htonl(start+1 + history_length);
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "window edge inside and move window");

    pipeline_object_unref(rec);
    free(pi);
    pipeline_unref(pl);
    OK(delete_packet(p) == NULL, "delete packet");
}

TEST_CASES = {
    {"no flags", test_no_flag},
    {"reset flag", test_reset_flag},
    {"init flag", test_init_flag},
    {NULL, NULL}
};

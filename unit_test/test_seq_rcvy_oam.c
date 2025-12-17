
#include "testing.h"

#include "action.h"
#include "notification.h"
#include "packet.h"
#include "pipeline.h"
#include "seq_recov.h"
#include "utils.h"

#include "log.h"

#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>

TEST_INIT("Sequence Recovery: OAM");

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
static const unsigned reset_ms = 300000; // way longer than OAM timeout so we know we test the OAM timeout

static void test_get_oam_rcvy(void)
{
    log_set_level("RCVY", DEBUG); //TODO to catch the flaky test in line 234
    struct RecoveryDiagnosticConf diag = {};
    struct PipelineObject *rec = new_seq_rec("oam", RCVY_Vector, false, false, history_length, reset_ms, &diag);
    OK_FATAL(rec, "have object");
    // session id string can be anything
    struct SequenceRecovery *rec1 = get_oam_rcvy(rec, "one of the recovery objects ever created");
    struct SequenceRecovery *rec2 = get_oam_rcvy(rec, "another nice recovery object");
    struct SequenceRecovery *rec3 = get_oam_rcvy(rec, "one of the recovery objects ever created");

    // the oam timeout is 5000 ms
    usleep(6000*1000);

    // note: without valgind the memory locations may be reused for the new sessions
    struct SequenceRecovery *rec4 = get_oam_rcvy(rec, "one of the recovery objects ever created");
    struct SequenceRecovery *rec5 = get_oam_rcvy(rec, "another nice recovery object");

    OK(rec1 != NULL && rec2 != NULL && rec3 != NULL && rec4 != NULL && rec5 != NULL, "shoud have every recovery");
    OK(rec2 != rec1, "different session");
    OK(rec3 == rec1, "same session");
    OK(rec4 != rec1, "new rcvy after reset");
    OK(rec5 != rec2, "new rcvy after reset");
    OK(rec4 != rec5, "different session after reset");

    pipeline_object_unref(rec);
}

static void test_single(void)
{
    //log_set_level("RCVY", PACKET);
    struct RecoveryDiagnosticConf diag = {};
    struct PipelineObject *rec = new_seq_rec("oam", RCVY_Vector, false, false, history_length, reset_ms, &diag);
    OK_FATAL(rec, "have object");

    struct Packet *p = new_packet(NULL);
    OK_FATAL(p, "have packet");

    // it must behave like match recovery: no history, just check that current seq differs from previous
    // (if current seq < previous seq that's out of order but not duplicate)

    OK(oam_recovery(rec, p, "sessionid", 8) == ACR_CONTINUE, "in TakeAny");
    OK(oam_recovery(rec, p, "sessionid", 8)== ACR_DONE, "duplicate");
    OK(oam_recovery(rec, p, "sessionid", 9) == ACR_CONTINUE, "not duplicate");
    OK(oam_recovery(rec, p, "sessionid", 9) == ACR_DONE, "duplicate");
    OK(oam_recovery(rec, p, "sessionid", 11) == ACR_CONTINUE, "not duplicate");
    OK(oam_recovery(rec, p, "sessionid", 11) == ACR_DONE, "duplicate");
    OK(oam_recovery(rec, p, "sessionid", 8) == ACR_CONTINUE, "not duplicate");
    OK(oam_recovery(rec, p, "sessionid", 8) == ACR_DONE, "duplicate");

    OK(oam_recovery(rec, p, "sessionid", 255) == ACR_CONTINUE, "not duplicate");
    OK(oam_recovery(rec, p, "sessionid", 255) == ACR_DONE, "duplicate");
    OK(oam_recovery(rec, p, "sessionid", 0) == ACR_CONTINUE, "not duplicate");
    OK(oam_recovery(rec, p, "sessionid", 0) == ACR_DONE, "duplicate");

    pipeline_object_unref(rec);
    OK(delete_packet(p) == NULL, "delete packet");
}

static void test_multi(void)
{
    // two sessions don't affect each other
    struct RecoveryDiagnosticConf diag = {};
    struct PipelineObject *rec = new_seq_rec("oam", RCVY_Vector, false, false, history_length, reset_ms, &diag);
    OK_FATAL(rec, "have object");

    struct Packet *p = new_packet(NULL);
    OK_FATAL(p, "have packet");

    OK(oam_recovery(rec, p, "session 1", 8) == ACR_CONTINUE, "in TakeAny");
    OK(oam_recovery(rec, p, "session 1", 8) == ACR_DONE, "duplicate");
    OK(oam_recovery(rec, p, "session 2", 8) == ACR_CONTINUE, "in TakeAny");
    OK(oam_recovery(rec, p, "session 2", 8) == ACR_DONE, "duplicate");
    OK(oam_recovery(rec, p, "session 1", 9) == ACR_CONTINUE, "not duplicate");
    OK(oam_recovery(rec, p, "session 1", 9) == ACR_DONE, "duplicate");
    OK(oam_recovery(rec, p, "session 2", 9) == ACR_CONTINUE, "not duplicate");
    OK(oam_recovery(rec, p, "session 2", 9) == ACR_DONE, "duplicate");
    OK(oam_recovery(rec, p, "session 1", 3) == ACR_CONTINUE, "not duplicate");
    OK(oam_recovery(rec, p, "session 1", 3) == ACR_DONE, "duplicate");
    OK(oam_recovery(rec, p, "session 2", 3) == ACR_CONTINUE, "not duplicate");
    OK(oam_recovery(rec, p, "session 2", 3) == ACR_DONE, "duplicate");

    pipeline_object_unref(rec);
    OK(delete_packet(p) == NULL, "delete packet");
}

static void test_timeout(void)
{
    struct RecoveryDiagnosticConf diag = {};
    struct PipelineObject *rec = new_seq_rec("match", RCVY_Match, false, false, history_length, reset_ms, &diag);
    OK_FATAL(rec, "have object");

    struct Packet *p = new_packet(NULL);
    OK_FATAL(p, "have packet");

    OK(oam_recovery(rec, p, "sessionid", 8) == ACR_CONTINUE, "in TakeAny");
    OK(oam_recovery(rec, p, "sessionid", 8) == ACR_DONE, "duplicate");

    // the oam timeout is 5000 ms
    usleep(6000*1000);

    OK(oam_recovery(rec, p, "sessionid", 8) == ACR_CONTINUE, "in TakeAny");
    OK(oam_recovery(rec, p, "sessionid", 8) == ACR_DONE, "duplicate");

    pipeline_object_unref(rec);
    OK(delete_packet(p) == NULL, "delete packet");
}

static void test_objects(void)
{
    struct RecoveryDiagnosticConf diag = {};
    struct PipelineObject *rec1 = new_seq_rec("oam 1", RCVY_Vector, false, false, history_length, reset_ms, &diag);
    struct PipelineObject *rec2 = new_seq_rec("oam 1", RCVY_Vector, false, false, history_length, reset_ms, &diag);
    OK_FATAL(rec1, "have object");
    OK_FATAL(rec2, "have object");

    struct Packet *p = new_packet(NULL);
    OK_FATAL(p, "have packet");

    OK(oam_recovery(rec1, p, "sessionid", 8) == ACR_CONTINUE, "in TakeAny");
    OK(oam_recovery(rec2, p, "sessionid", 8) == ACR_CONTINUE, "in TakeAny");
    OK(oam_recovery(rec1, p, "sessionid", 8) == ACR_DONE, "duplicate");
    OK(oam_recovery(rec2, p, "sessionid", 8) == ACR_DONE, "duplicate");

    OK(oam_recovery(rec1, p, "sessionid", 9) == ACR_CONTINUE, "not duplicate");
    OK(oam_recovery(rec1, p, "sessionid", 9) == ACR_DONE, "duplicate");
    OK(oam_recovery(rec2, p, "sessionid", 9) == ACR_CONTINUE, "not duplicate");
    OK(oam_recovery(rec2, p, "sessionid", 9) == ACR_DONE, "duplicate");

    pipeline_object_unref(rec1);
    pipeline_object_unref(rec2);
    OK(delete_packet(p) == NULL, "delete packet");
}

TEST_CASES = {
    {"get oam rcvy", test_get_oam_rcvy},
    {"single session", test_single},
    {"multiple sessions", test_multi},
    {"timeout", test_timeout},
    {"multiple objects", test_objects},
    {NULL, NULL}
};

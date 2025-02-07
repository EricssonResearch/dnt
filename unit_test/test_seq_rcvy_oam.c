
#include "testing.h"

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
bool notification_push_event(const char *source, NotificationLevel level, struct JsonValue *message) {
    (void)source; (void)level; (void)message; return false;
}
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

static void prepare_oam_packet(struct Packet *p, unsigned short nodeid, unsigned char session)
{
    p->len = 30;
    OK(packet_identify_header(p, PROTO_ID_IPv6, 0, 10) == true, "first header doesn't matter");
    OK(packet_identify_header(p, PROTO_ID_OAM, 10, 8) == true, "oam header");

    unsigned char *oam  = p->buf + p->headers[1].start;
    oam[4] = (nodeid>>8) & 0xff;
    oam[5] = nodeid & 0xff;
    oam[7] = session & 0x0f;
}

static void test_get_sessionid(void)
{
    struct Packet *p = new_packet(NULL);
    OK_FATAL(p, "have packet");

    // session id is 4 bit so this becomes 11
    prepare_oam_packet(p, 1234, 91);

    char *s = oam_session_id(p);
    OK(s != NULL, "have session id");
    OK(strcmp(s, "1234:11") == 0, "session id '%s'", s);
    free(s);

    delete_packet(p);
}

static void test_get_oam_rcvy(void)
{
    struct SequenceRecovery *rec1 = get_oam_rcvy("one of the recovery objects ever created");
    struct SequenceRecovery *rec2 = get_oam_rcvy("another nice recovery object");
    struct SequenceRecovery *rec3 = get_oam_rcvy("one of the recovery objects ever created");

    // the oam timeout is 5000 ms
    usleep(6000*1000);

    struct SequenceRecovery *rec4 = get_oam_rcvy("one of the recovery objects ever created");
    struct SequenceRecovery *rec5 = get_oam_rcvy("another nice recovery object");

    OK(rec1 != NULL && rec2 != NULL && rec3 != NULL && rec4 != NULL && rec5 != NULL, "shoud have every recovery");
    OK(rec2 != rec1, "different session");
    OK(rec3 == rec1, "same session");
    OK(rec4 != rec1, "new rcvy after reset");
    OK(rec5 != rec2, "new rcvy after reset");
    OK(rec4 != rec5, "different session after reset");
}

static void test_single(void)
{
    //log_set_level("RCVY", PACKET);
    struct PipelineObject *rec = new_seq_rec("oam", RCVY_Vector, false, false, history_length, reset_ms, NULL);
    OK_FATAL(rec, "have object");

    struct Packet *p = new_packet(NULL);
    OK_FATAL(p, "have packet");
    struct Pipeline *pl = new_pipeline("test");
    OK_FATAL(pl, "have pipeline");
    struct PipelineIterator *pi = new_pipe_iterator(pl, p);
    OK_FATAL(pi, "have iterator");

    prepare_oam_packet(p, 1234, 1);
    unsigned char *meta = ((unsigned char *)(&p->sequence));
    meta[0] = 0x11; // indicator and version
    unsigned char *seq = meta+1;

    // it must behave like match recovery: no history, just check that current seq differs from previous
    // (if current seq < previous seq that's out of order but not duplicate)

    *seq = 8;
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "in TakeAny");
    OK(rec->process_packet(rec, pi) == ACR_DONE, "duplicate");
    *seq = 9;
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "not duplicate");
    OK(rec->process_packet(rec, pi) == ACR_DONE, "duplicate");
    *seq = 11;
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "not duplicate");
    OK(rec->process_packet(rec, pi) == ACR_DONE, "duplicate");
    *seq = 8;
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "not duplicate");
    OK(rec->process_packet(rec, pi) == ACR_DONE, "duplicate");

    *seq = 255;
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "not duplicate");
    OK(rec->process_packet(rec, pi) == ACR_DONE, "duplicate");
    *seq = 0;
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "not duplicate");
    OK(rec->process_packet(rec, pi) == ACR_DONE, "duplicate");

    OK(delete_seq_rec(rec) == NULL, "delete object");
    free(pi);
    pipeline_unref(pl);
    OK(delete_packet(p) == NULL, "delete packet");
}

static void test_multi(void)
{
    // two sessions don't affect each other
    struct PipelineObject *rec = new_seq_rec("oam", RCVY_Vector, false, false, history_length, reset_ms, NULL);
    OK_FATAL(rec, "have object");

    struct Packet *p1 = new_packet(NULL);
    OK_FATAL(p1, "have packet");
    struct Pipeline *pl1 = new_pipeline("test");
    OK_FATAL(pl1, "have pipeline");
    struct PipelineIterator *pi1 = new_pipe_iterator(pl1, p1);
    OK_FATAL(pi1, "have iterator");
    struct Packet *p2 = new_packet(NULL);
    OK_FATAL(p2, "have packet");
    struct Pipeline *pl2 = new_pipeline("test");
    OK_FATAL(pl2, "have pipeline");
    struct PipelineIterator *pi2 = new_pipe_iterator(pl2, p2);
    OK_FATAL(pi2, "have iterator");

    prepare_oam_packet(p1, 234, 1);
    unsigned char *meta1 = ((unsigned char *)(&p1->sequence));
    meta1[0] = 0x11; // indicator and version
    unsigned char *seq1 = meta1+1;
    prepare_oam_packet(p2, 234, 2);
    unsigned char *meta2 = ((unsigned char *)(&p2->sequence));
    meta2[0] = 0x11; // indicator and version
    unsigned char *seq2 = meta2+1;

    *seq1 = 8;
    *seq2 = 8;
    OK(rec->process_packet(rec, pi1) == ACR_CONTINUE, "in TakeAny");
    OK(rec->process_packet(rec, pi1) == ACR_DONE, "duplicate");
    OK(rec->process_packet(rec, pi2) == ACR_CONTINUE, "in TakeAny");
    OK(rec->process_packet(rec, pi2) == ACR_DONE, "duplicate");
    *seq1 = 9;
    *seq2 = 9;
    OK(rec->process_packet(rec, pi1) == ACR_CONTINUE, "not duplicate");
    OK(rec->process_packet(rec, pi1) == ACR_DONE, "duplicate");
    OK(rec->process_packet(rec, pi2) == ACR_CONTINUE, "not duplicate");
    OK(rec->process_packet(rec, pi2) == ACR_DONE, "duplicate");
    *seq1 = 3;
    *seq2 = 3;
    OK(rec->process_packet(rec, pi1) == ACR_CONTINUE, "not duplicate");
    OK(rec->process_packet(rec, pi1) == ACR_DONE, "duplicate");
    OK(rec->process_packet(rec, pi2) == ACR_CONTINUE, "not duplicate");
    OK(rec->process_packet(rec, pi2) == ACR_DONE, "duplicate");

    OK(delete_seq_rec(rec) == NULL, "delete object");
    free(pi1);
    free(pi2);
    pipeline_unref(pl1);
    pipeline_unref(pl2);
    OK(delete_packet(p1) == NULL, "delete packet");
    OK(delete_packet(p2) == NULL, "delete packet");
}

static void test_timeout(void)
{
    struct PipelineObject *rec = new_seq_rec("match", RCVY_Match, false, false, history_length, reset_ms, NULL);
    OK_FATAL(rec, "have object");

    struct Packet *p = new_packet(NULL);
    OK_FATAL(p, "have packet");
    struct Pipeline *pl = new_pipeline("test");
    OK_FATAL(pl, "have pipeline");
    struct PipelineIterator *pi = new_pipe_iterator(pl, p);
    OK_FATAL(pi, "have iterator");

    prepare_oam_packet(p, 34, 1);
    unsigned char *meta = ((unsigned char *)(&p->sequence));
    meta[0] = 0x11; // indicator and version
    unsigned char *seq = meta+1;

    *seq = 8;
    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "in TakeAny");
    OK(rec->process_packet(rec, pi) == ACR_DONE, "duplicate");

    // the oam timeout is 5000 ms
    usleep(6000*1000);

    OK(rec->process_packet(rec, pi) == ACR_CONTINUE, "in TakeAny");
    OK(rec->process_packet(rec, pi) == ACR_DONE, "duplicate");

    OK(delete_seq_rec(rec) == NULL, "delete object");
    free(pi);
    pipeline_unref(pl);
    OK(delete_packet(p) == NULL, "delete packet");
}

TEST_CASES = {
    {"get session id", test_get_sessionid},
    {"get oam rcvy", test_get_oam_rcvy},
    {"single session", test_single},
    {"multiple sessions", test_multi},
    {"timeout", test_timeout},
    {NULL, NULL}
};

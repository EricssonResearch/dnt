
#include "testing.h"

#include "delay.h"
#include "action.h"
#include "interface.h"
#include "log.h"
#include "notification.h"
#include "oam.h"
#include "thread_utils.h"
#include "utils.h"

#include <pthread.h>

#include <valgrind/valgrind.h>

TEST_INIT("Delay");

// XXX stubs for stuff that we transitively depend on but don't need
const char *ether_ntop(const void *src, char *dst, unsigned dstsize);
const char *ether_ntop(const void *src, char *dst, unsigned dstsize) { (void)src; (void)dst; (void)dstsize; return NULL; }
void iface_add_sender(struct Interface *iface) { (void)iface; }
void iface_del_sender(struct Interface *iface) { (void)iface; }
bool notification_register_source(const char *name, notification_pull_fn *callback, void *self, unsigned period_ms);
bool notification_register_source(const char *name, notification_pull_fn *callback, void *self, unsigned period_ms)
    { (void)name; (void)callback; (void)self; (void)period_ms; return false; }
bool notification_push_event(const char *source, NotificationLevel level, struct JsonValue *message);
bool notification_push_event(const char *source, NotificationLevel level, struct JsonValue *message)
    { (void)source; (void)level; json_delete(message); return false; }
struct OAM_MaintenancePoint *oam_new_maintenance_point(const char *stream_name, const char *mp_name,
        enum OAM_MP_Type type, unsigned level,
        const enum ProtocolID *protostack,
        struct PipelineObject *obj, struct Pipeline *pipe, unsigned idx,
        struct OAM_MP_Address *addr)
    { (void)stream_name; (void)mp_name; (void)type; (void)level;
        (void)protostack; (void)obj; (void)pipe; (void)idx; (void)addr; return NULL; }
void oam_unref_maintenance_point(struct OAM_MaintenancePoint *mp) { (void)mp; }
void oam_mp_count_data_packet(struct OAM_MaintenancePoint *mp, unsigned len) { (void)mp; (void)len; }
void oam_pipeline_deleted(struct Pipeline *pipe) { (void)pipe; }
void oam_receive_inband(struct OAM_MaintenancePoint *mp, struct PipelineIterator *pi) { (void)mp; (void)pi; }
enum ActionResult oam_recovery(struct PipelineObject *obj, struct Packet *p, const char *session_id, unsigned char seq);
enum ActionResult oam_recovery(struct PipelineObject *obj, struct Packet *p, const char *session_id, unsigned char seq)
    { (void)obj; (void)p; (void)session_id; (void)seq; return ACR_DONE; }
void pipeline_object_ref(struct PipelineObject *obj);
void pipeline_object_ref(struct PipelineObject *obj) { (void)obj; }
void pipeline_object_unref(struct PipelineObject *obj);
void pipeline_object_unref(struct PipelineObject *obj) { (void)obj; }
const struct Protocol *protocol_from_id(enum ProtocolID id) { (void)id; return NULL; }
const struct ProtocolField *protocol_get_field_by_type(enum ProtocolID id, enum ProtocolFieldType type)
    { (void)id; (void)type; return NULL; }
void store_replication_pipelines(struct PipelineObject *obj, struct PipelineList *pipes);
void store_replication_pipelines(struct PipelineObject *obj, struct PipelineList *pipes) { (void)obj; (void)pipes; }
// XXX end stubs

static unsigned timerif_count = 0;
static bool timerif_send(struct Interface *iface, struct Packet *p)
{
    struct timespec *recvtimes = (struct timespec *)iface->iface_private;
    struct timespec now;
    clock_gettime(DELAY_CLOCK, &now);
    recvtimes[p->sequence] = now;
    timerif_count++;
    printf("-");
    return true;
}

static void run_singlethread(bool verify_results)
{
    // note: we deliberately only initialize the fields that are expected to be used
    //       let's see if Valgrind detects uninitialized read

    // sending is expected at now - packet_offset + delay
    struct Offset {
        struct timespec packet_offset;
        struct timespec delay;
    } offsets[] = {
        // with valgrind we need the first batch to be -25, otherwise even -50 is okay
        { {0, 20*1000*1000}, {0, 51*1000*1000} },
        { {0, 20*1000*1000}, {0, 59*1000*1000} },
        { {0, 20*1000*1000}, {0, 56*1000*1000} },
        { {0, 20*1000*1000}, {0, 60*1000*1000} },
        { {0, 20*1000*1000}, {0, 53*1000*1000} },
        { {0, 10*1000*1000}, {0, 51*1000*1000} },
        { {0, 10*1000*1000}, {0, 59*1000*1000} },
        { {0, 10*1000*1000}, {0, 56*1000*1000} },
        { {0, 10*1000*1000}, {0, 60*1000*1000} },
        { {0, 10*1000*1000}, {0, 53*1000*1000} },
        { {0, 15*1000*1000}, {0, 51*1000*1000} },
        { {0, 15*1000*1000}, {0, 59*1000*1000} },
        { {0, 15*1000*1000}, {0, 56*1000*1000} },
        { {0, 15*1000*1000}, {0, 60*1000*1000} },
        { {0, 15*1000*1000}, {0, 53*1000*1000} },
    };

    struct timespec recvtimes[ARRAY_SIZE(offsets)];
    struct timespec expecteds[ARRAY_SIZE(offsets)];

    OK_FATAL(init_delay(), "init delay");

    struct Interface timerif;
    timerif.send = timerif_send;
    timerif.iface_private = recvtimes;
    timerif_count = 0;

    struct Pipeline pipe;
    pipe.action_count = 5;
    pipe.actions = (struct Action*)malloc(5*sizeof(struct Action));
    pipe.name = (char*)"testing single pipe";
    pipe.reference_count = 1;
    // this must be the last action in the array
    create_action_send(&pipe.actions[4], &timerif, "send on timerif");

    for (unsigned i=0; i<ARRAY_SIZE(offsets); i++) {
        struct Packet *p = new_packet_fast(NULL);
        OK_FATAL(p, "have packet %u", i);
        p->sequence = i;
        struct timespec now;
        clock_gettime(DELAY_CLOCK, &now);

        struct timespec packet_enter;
        timespecsub(&now, &offsets[i].packet_offset, &packet_enter);
        timespec_to_tsntstamp(p->timestamp, &packet_enter);
        /*printf("\n%u now %lu.%.09lu packet_enter %lu.%.09lu", i,
                now.tv_sec, now.tv_nsec,
                packet_enter.tv_sec, packet_enter.tv_nsec);*/

        p->delay = offsets[i].delay;
        timespecadd(&packet_enter, &offsets[i].delay, &expecteds[i]);

        struct PipelineIterator *pi = new_pipe_iterator(&pipe, p);
        OK_FATAL(pi, "have iterator %u", i);
        pi->pos_ = 3; // the one before send

        delay_insert(pi);
    }

    if (verify_results) {
        // wait for the packets to arrive
        usleep(1000*1000);

        for (unsigned i=0; i<ARRAY_SIZE(offsets); i++) {
            int64_t diff = time_diff_us(recvtimes[i], expecteds[i]);
            printf("\n%u recv %lu.%.09lu expected %lu.%.09lu diff %ld", i,
              recvtimes[i].tv_sec, recvtimes[i].tv_nsec,
              expecteds[i].tv_sec, expecteds[i].tv_nsec,
              diff);
            int threshold = 400; // should be high enough even for valgrind
            // in valgrind the first dequeue is always very late, the rest are okay
            if (RUNNING_ON_VALGRIND && i == 0)
                threshold = 30000;
            OK(diff < threshold && diff > -threshold, "%u recv %lu.%.09lu expected %lu.%.09lu diff %ld", i,
                    recvtimes[i].tv_sec, recvtimes[i].tv_nsec,
                    expecteds[i].tv_sec, expecteds[i].tv_nsec,
                    diff);
        }
    }

    finish_delay();
    OK(timerif_count == ARRAY_SIZE(offsets), "timerif_count %u", timerif_count);
    delete_action(&pipe.actions[4]);
    free(pipe.actions);
}

static void test_delays(void)
{
    run_singlethread(true);
}

static void test_flush(void)
{
    // same as test_delays() but we don't wait before cleaning up the queue
    //      here we call finish_delay() while the queue is not empty
    //      expectation: queue is flushed properly, no packet is lost
    run_singlethread(false);
}

static int count_total_packets(const char *pipe, uint64_t packets, uint64_t delay_exceeded, void *userdata)
{
    (void)pipe;
    (void)delay_exceeded;
    int64_t *count = (int64_t *)userdata;
    count[0] += packets;
    count[1] += delay_exceeded;
    return 1;
}

#define ITERATIONS 500
static pthread_spinlock_t spinlock;

struct multi_arg {
    struct Pipeline *pipe;
    struct timespec *expecteds;
    unsigned start_idx;
};

static bool multi_push_packet(struct Pipeline *pipe, unsigned seq, struct timespec *expecteds)
{
    // note: testing.h is not thread-safe, we can't use OK() here

    struct Packet *p = new_packet_fast(NULL);
    if (p == NULL) return false;
    p->sequence = seq;
    struct timespec now;
    clock_gettime(DELAY_CLOCK, &now);

    // packets enter now - [30..50]ms
    // delay is 50..130ms
    // the deadline is thus now + [0..100]ms
    // (with deadline=0ms we test being late)
    struct timespec packet_enter = now;
    packet_enter.tv_sec -= 1;
    packet_enter = time_add_us(packet_enter, 950*1000 + (rand() % (20*1000)));
    timespec_to_tsntstamp(p->timestamp, &packet_enter);

    struct timespec random_delay;
    random_delay.tv_sec = 0;
    random_delay.tv_nsec = 50*1000*1000 + (rand() % (80*1000*1000));
    p->delay = random_delay;
    timespecadd(&packet_enter, &random_delay, &expecteds[seq]);

    /*printf("\nnow %lu.%.09lu packet enter %lu.%.09lu delay %lu.%.09lu",
            now.tv_sec, now.tv_nsec,
            packet_enter.tv_sec, packet_enter.tv_nsec,
            random_delay.tv_sec, random_delay.tv_nsec);*/

    struct PipelineIterator *pi = new_pipe_iterator(pipe, p);
    if (pi == NULL) return false;
    pi->pos_ = 3; // the one before send

    delay_insert(pi);
    printf("+");
    return true;
}

static void *multi_thread(void *arg)
{
    // note: testing.h is not thread-safe, we can't use OK() here

    struct multi_arg *marg = (struct multi_arg *)arg;

    for (unsigned i=0; i<ITERATIONS; i++) {
        pthread_spin_lock(&spinlock);

        multi_push_packet(marg->pipe, marg->start_idx + i, marg->expecteds);

        usleep(10*1000);
        /*volatile int k = 0;
        for (unsigned a=0; a<10000000;a++) {
            k += a;
        }*/

        pthread_spin_unlock(&spinlock);

        multi_push_packet(marg->pipe, marg->start_idx + i + ITERATIONS, marg->expecteds);
    }

    return NULL;
}

// two threads pushing into the delay queue
static void test_multi(void)
{
    if (RUNNING_ON_VALGRIND)
        SKIP("Valgrind doesn't properly support multithreading");

    OK_FATAL(pthread_spin_init(&spinlock, 0) == 0, "create spinlock");

    OK_FATAL(init_delay(), "init delay");

    srand(42); //TODO this doesn't ensure consistency :(

    // we have 2 threads and each will generate ITERATIONS*2 packets
    struct timespec recvtimes[ITERATIONS*2*2];
    struct timespec expecteds[ITERATIONS*2*2];

    struct Interface timerif;
    timerif.send = timerif_send;
    timerif.iface_private = recvtimes;
    timerif_count = 0;

    struct Pipeline pipe;
    pipe.action_count = 5;
    pipe.actions = (struct Action*)malloc(5*sizeof(struct Action));
    pipe.name = (char*)"testing multi pipe";
    pipe.reference_count = 1;
    // this must be the last action in the array
    create_action_send(&pipe.actions[4], &timerif, "send on timerif");

    struct multi_arg targs[] = {
        {&pipe, expecteds, 0},
        {&pipe, expecteds, ITERATIONS*2},
    };

    struct Thread *t[2];
    for (unsigned i=0; i<2; i++) {
        t[i] = thread_launch(multi_thread, &targs[i], "delay %u", i);
    }

    for (unsigned i=0; i<2; i++) {
        thread_join(t[i]);
    }

    // wait for the packets to arrive
    usleep(1000*1000);

    // the expected timing accuracy is 1ms
    for (unsigned i=0; i<ARRAY_SIZE(recvtimes); i++) {
        int64_t diff = time_diff_us(recvtimes[i], expecteds[i]);
        /*printf("\n%u recv %lu.%.09lu expected %lu.%.09lu diff %ld", i,
                recvtimes[i].tv_sec, recvtimes[i].tv_nsec,
                expecteds[i].tv_sec, expecteds[i].tv_nsec,
                diff);*/
        OK(diff < 1000 && diff > -1000, "%u recv %lu.%.09lu expected %lu.%.09lu diff %ld", i,
                recvtimes[i].tv_sec, recvtimes[i].tv_nsec,
                expecteds[i].tv_sec, expecteds[i].tv_nsec,
                diff);
    }

    uint64_t stat_packets[2] = {0, 0}; // totals: processed, delay exceeded
    OK(foreach_delay_stat(count_total_packets, &stat_packets), "foreach interrupted");
    OK(stat_packets[0] == ARRAY_SIZE(recvtimes), "packet count %lu", stat_packets[0]);
    // we expect some to be late because we can have 0 deadline
    // unfortunately the result is not exact :(
    OK(stat_packets[1] > 0, "exceeded count %lu   ", stat_packets[1]);
    OK(timerif_count == ARRAY_SIZE(recvtimes), "timerif_count %u", timerif_count);

    finish_delay();
    delete_action(&pipe.actions[4]);
    free(pipe.actions);
    pthread_spin_destroy(&spinlock);
}

static void *multinoq_master(void *arg)
{
    // note: testing.h is not thread-safe, we can't use OK() here

    struct multi_arg *marg = (struct multi_arg *)arg;

    for (unsigned i=0; i<ITERATIONS; i++) {
        pthread_spin_lock(&spinlock);
        init_delay();
        usleep(40*1000);
        pthread_spin_unlock(&spinlock);
        multi_push_packet(marg->pipe, marg->start_idx + i, marg->expecteds);
        usleep(20*1000);
        finish_delay();
    }

    return NULL;
}

static void *multinoq_slave(void *arg)
{
    // note: testing.h is not thread-safe, we can't use OK() here

    struct multi_arg *marg = (struct multi_arg *)arg;

    for (unsigned i=0; i<ITERATIONS; i++) {
        usleep(40*1000);
        pthread_spin_lock(&spinlock);
        multi_push_packet(marg->pipe, marg->start_idx + i, marg->expecteds);
        pthread_spin_unlock(&spinlock);
    }


    return NULL;
}

// synchronized push when no queue is running to test the thread startup
static void test_multinoq(void)
{
    if (RUNNING_ON_VALGRIND)
        SKIP("Valgrind doesn't properly support multithreading");

    // stop the spam
    log_set_level("DELAY", ERROR);
    log_set_level("THREAD", NONE);

    OK_FATAL(pthread_spin_init(&spinlock, 0) == 0, "create spinlock");

    struct timespec recvtimes[ITERATIONS*2];
    struct timespec expecteds[ITERATIONS*2];

    struct Interface timerif;
    timerif.send = timerif_send;
    timerif.iface_private = recvtimes;
    timerif_count = 0;

    struct Pipeline pipe;
    pipe.action_count = 5;
    pipe.actions = (struct Action*)malloc(5*sizeof(struct Action));
    pipe.name = (char*)"testing multi noq pipe";
    pipe.reference_count = 1;
    // this must be the last action in the array
    create_action_send(&pipe.actions[4], &timerif, "send on timerif");

    struct timespec now;
    clock_gettime(DELAY_CLOCK, &now);

    struct multi_arg targs[] = {
        {&pipe, expecteds, 0},
        {&pipe, expecteds, ITERATIONS},
    };

    struct Thread *t[2];
    t[0] = thread_launch(multinoq_master, &targs[0], "delay master");
    t[1] = thread_launch(multinoq_slave, &targs[1], "delay slave");

    for (unsigned i=0; i<2; i++) {
        thread_join(t[i]);
    }

    // the packets are sent on flush, so the receive times are not the expected
    // here we only check that they have reasonable times
    for (unsigned i=0; i<ARRAY_SIZE(recvtimes); i++) {
        OK(recvtimes[i].tv_sec >= now.tv_sec, "recv %u %ld.%.09ld",
                i, recvtimes[i].tv_sec, recvtimes[i].tv_nsec);
    }

    OK(timerif_count == ARRAY_SIZE(recvtimes), "timerif_count %u", timerif_count);

    delete_action(&pipe.actions[4]);
    free(pipe.actions);
    pthread_spin_destroy(&spinlock);
}

TEST_CASES = {
    {"delays", test_delays},
    {"flush", test_flush},
    {"multi-thread push", test_multi},
    {"multi-thread push when no queue", test_multinoq},
    {NULL, NULL}
};


#include "testing.h"

#include "thread_utils.h"
#include "log.h"
#include "time_utils.h"

#include <stdlib.h>
#include <string.h>

TEST_INIT("Thread Utils");

//TODO we have to be careful, testing.h is not thread-safe!

struct ThreadTestParam {
    int counter;
    struct Thread *th;
    struct MessageQueue *mq;
};

static void *thread_func(void *arg)
{
    struct ThreadTestParam *param = (struct ThreadTestParam *)arg;
    param->counter++;
    while (1) { usleep(1000*1000); }
    return NULL;
}

static void *thread_exit_func(void *arg)
{
    struct ThreadTestParam *param = (struct ThreadTestParam *)arg;
    usleep(1000*100); // make sure the assignment to param->th is done
    thread_stop(param->th); // should do nothing
    thread_join(param->th); // should do nothing
    param->counter = 21;
    thread_exit(param->th);
    return NULL;
}

static void *thread_join_func(void *arg)
{
    struct ThreadTestParam *param = (struct ThreadTestParam *)arg;
    void *msg = messagequeue_pop(param->mq, -1);
    OK(msg == param, "msg %p param %p", msg, param);
    usleep(500*1000);
    param->mq = delete_messagequeue(param->mq);
    return NULL;
}

static void test_thread(void)
{
    struct ThreadTestParam param1;
    param1.counter = 0;
    param1.th = thread_launch(thread_func, &param1, "test thread %d", param1.counter);
    OK_FATAL(param1.th, "have thread object");
    unsigned id1 = thread_getid(param1.th);
    usleep(1000*500); // wait for the thread
    OK(param1.counter == 1, "thread worked");
    OK(strcmp(thread_getname(param1.th), "test thread 0") == 0, "good name");
    param1.th = thread_stop(param1.th);
    OK_FATAL(param1.th == NULL, "thread object gone");

    // thread_stop() doesn't do anything to self
    struct ThreadTestParam param2;
    param2.counter = 0;
    param2.th = thread_launch(thread_exit_func, &param2, "test exit");
    OK_FATAL(param2.th, "have thread object");
    unsigned id2 = thread_getid(param2.th);
    usleep(1000*1000); // wait for the thread
    OK(param2.counter == 21, "thread_stop didn't stop itself");

    // thread_exit() doesn't do anything to others
    struct ThreadTestParam param3;
    param3.counter = 0;
    param3.th = thread_launch(thread_exit_func, &param3, "test exit again");
    OK_FATAL(param3.th, "have thread object");
    unsigned id3 = thread_getid(param3.th);
    thread_exit(param3.th); // should do nothing
    usleep(1000*1000); // wait for the thread
    OK(param3.counter == 21, "thread_exit didn't stop the thread");

    // thread_join() waits for the thread to finish
    struct ThreadTestParam param4;
    param4.counter = 0;
    param4.mq = new_messagequeue();
    OK_FATAL(param4.mq, "have queue");
    param4.th = thread_launch(thread_join_func, &param4, "test join");
    OK_FATAL(param4.th, "have thread object");
    messagequeue_push(param4.mq, &param4); // signal the thread to finish its job and quit
    OK(param4.mq, "have queue");
    struct timespec time1;
    clock_gettime(CLOCK_REALTIME, &time1);
    param4.th = thread_join(param4.th);
    OK(param4.th == NULL, "always returns NULL");
    OK(param4.mq == NULL, "queue gone");
    struct timespec time2;
    clock_gettime(CLOCK_REALTIME, &time2);
    int64_t diff_us = time_diff_us(time2, time1);
    OK(diff_us > 400*1000, "diff %ld", diff_us);

    OK(id1 != id2, "different id");
    OK(id1 != id3, "different id");
    OK(id2 != id3, "different id");

    OK(thread_stop(NULL) == NULL, "stop NULL thread");
    thread_exit(NULL); // exit NULL thread shouldn't segfault either

    log_set_level("THREAD", NONE);
    struct ThreadTestParam param5;
    param5.counter = 0;
    param5.th = thread_launch_priority(thread_func, &param5, 10, "test thread %s", "p");
    if (param5.th) {
        usleep(1000*500); // wait for the thread
        OK(param5.counter == 1, "priority thread worked");
        OK(strcmp(thread_getname(param5.th), "test thread p") == 0, "good name");
        param5.th = thread_stop(param5.th);
        OK_FATAL(param5.th == NULL, "thread object gone");
    } else {
        SKIP("need CAP_SYS_NICE for the thread priority test");
    }
}

struct ThreadMQParam {
    int counter;
    int timeouts;
    int errors;
    struct MessageQueue *q;
};

static void *thread_mq_timeout_func(void *arg)
{
    struct ThreadMQParam *param = (struct ThreadMQParam *)arg;

    while (1) {
        struct ThreadTestParam *message = (struct ThreadTestParam *)messagequeue_pop(param->q, 1000*800);
        if (message) {
            param->counter += message->counter;
            free(message);
        } else {
            param->timeouts++;
        }
    }

    return NULL;
}

static void *thread_mq_immediate_func(void *arg)
{
    struct ThreadMQParam *param = (struct ThreadMQParam *)arg;

    while (1) {
        struct ThreadTestParam *message = (struct ThreadTestParam *)messagequeue_pop(param->q, 0);
        if (message) {
            param->counter += message->counter;
            free(message);
        } else {
            param->timeouts++;
            usleep(1000*500);
        }
    }

    return NULL;
}

static void *thread_mq_notimeout_func(void *arg)
{
    struct ThreadMQParam *param = (struct ThreadMQParam *)arg;

    while (1) {
        struct ThreadTestParam *message = (struct ThreadTestParam *)messagequeue_pop(param->q, -1);
        if (message) {
            param->counter += message->counter;
            free(message);
        } else {
            param->timeouts++;
        }
    }

    return NULL;
}

static void *thread_mq_multi_func(void *arg)
{
    struct ThreadMQParam *param = (struct ThreadMQParam *)arg;

    while (1) {
        struct ThreadTestParam *message = (struct ThreadTestParam *)messagequeue_pop(param->q, 1000*200);
        if (message) {
            if (message->counter != param->counter + 1) {
                param->errors++;
            }
            param->counter = message->counter;
            free(message);
        } else {
            param->timeouts++;
        }
    }

    return NULL;
}

static void test_mq(void)
{
    struct ThreadMQParam param1;
    param1.counter = 0;
    param1.timeouts = 0;
    param1.errors = 0;
    param1.q = new_messagequeue();
    OK_FATAL(param1.q, "have queue");
    struct Thread *th = thread_launch(thread_mq_timeout_func, &param1, "test mq timeout");
    OK_FATAL(th, "have thread object");

    usleep(1000*1200); // thread timeout is 500
    OK(param1.counter == 0, "counter %d", param1.counter);
    OK(param1.timeouts == 1, "timeouts %d", param1.timeouts);

    struct ThreadTestParam *message;
    message = (struct ThreadTestParam *)malloc(sizeof(struct ThreadTestParam));
    message->counter = 4;
    messagequeue_push(param1.q, message);
    message = (struct ThreadTestParam *)malloc(sizeof(struct ThreadTestParam));
    message->counter = 3;
    messagequeue_push(param1.q, message);
    usleep(1000*300);
    OK(param1.counter == 7, "counter %d", param1.counter);
    OK(param1.timeouts == 1, "timeouts %d", param1.timeouts);

    usleep(1000*1200); // thread timeout is 500
    OK(param1.counter == 7, "counter %d", param1.counter);
    OK(param1.timeouts == 2, "timeouts %d", param1.timeouts);

    //usleep(1000*30000); // to check the thread name in htop

    th = thread_stop(th);
    OK_FATAL(th == NULL, "thread object gone");

    struct ThreadMQParam param2;
    param2.counter = 0;
    param2.timeouts = 0;
    param2.errors = 0;
    param2.q = new_messagequeue();
    OK_FATAL(param2.q, "have queue");
    th = thread_launch(thread_mq_immediate_func, &param2, "test mq immediate");
    OK_FATAL(th, "have thread object");
    usleep(1000*1300);
    OK(param2.counter == 0, "counter %d", param2.counter);
    OK(param2.timeouts == 3, "timeouts %d", param2.timeouts); // at 0, 500, 1000
    message = (struct ThreadTestParam *)malloc(sizeof(struct ThreadTestParam));
    message->counter = 9;
    messagequeue_push(param2.q, message);
    usleep(1000*500);
    OK(param2.counter == 9, "counter %d", param2.counter);
    OK(param2.timeouts == 4, "timeouts %d", param2.timeouts); // at 1500

    th = thread_stop(th);
    OK_FATAL(th == NULL, "thread object gone");

    struct ThreadMQParam param3;
    param3.counter = 0;
    param3.timeouts = 0;
    param3.errors = 0;
    param3.q = new_messagequeue();
    OK_FATAL(param3.q, "have queue");
    th = thread_launch(thread_mq_notimeout_func, &param3, "test mq notimeout");
    OK_FATAL(th, "have thread object");
    usleep(1000*500);
    OK(param3.counter == 0, "counter %d", param3.counter);
    OK(param3.timeouts == 0, "timeouts %d", param3.timeouts);
    message = (struct ThreadTestParam *)malloc(sizeof(struct ThreadTestParam));
    message->counter = 5;
    messagequeue_push(param3.q, message);
    usleep(1000*200);
    OK(param3.counter == 5, "counter %d", param3.counter);
    OK(param3.timeouts == 0, "timeouts %d", param3.timeouts);

    th = thread_stop(th);
    OK_FATAL(th == NULL, "thread object gone");

    struct ThreadMQParam param4;
    param4.counter = 0;
    param4.timeouts = 0;
    param4.errors = 0;
    param4.q = new_messagequeue();
    OK_FATAL(param4.q, "have queue");

    // test pushing multiple items
    for (int i=1; i<20; i++) {
        message = (struct ThreadTestParam *)malloc(sizeof(struct ThreadTestParam));
        message->counter = i;
        messagequeue_push(param4.q, message);
    }
    th = thread_launch(thread_mq_multi_func, &param4, "test mq multiple msg");
    OK_FATAL(th, "have thread object");
    usleep(1000*100);
    OK(param4.counter == 19, "counter %d", param4.counter);
    OK(param4.timeouts == 0, "timeouts %d", param4.timeouts);
    OK(param4.errors == 0, "errors %d", param4.errors);

    th = thread_stop(th);
    OK_FATAL(th == NULL, "thread object gone");

    param1.q = delete_messagequeue(param1.q);
    OK(param1.q == NULL, "queue gone");
    param2.q = delete_messagequeue(param2.q);
    OK(param2.q == NULL, "queue gone");
    param3.q = delete_messagequeue(param3.q);
    OK(param3.q == NULL, "queue gone");
    param4.q = delete_messagequeue(param4.q);
    OK(param4.q == NULL, "queue gone");

    OK(delete_messagequeue(NULL) == NULL, "delete NULL queue");
}

TEST_CASES = {
    {"thread", test_thread},
    {"message queue", test_mq},
    {NULL, NULL}
};

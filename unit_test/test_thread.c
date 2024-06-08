
#include "testing.h"

#include "thread_utils.h"

#include <stdlib.h>
#include <string.h>

TEST_INIT("Thread Utils");

//TODO we have to be careful, testing.h is not thread-safe!

struct ThreadTestParam {
    int counter;
    struct Thread *th;
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
    usleep(2000); // make sure the assignment to param->th is done
    thread_stop(param->th); // should do nothing
    param->counter = 21;
    thread_exit(param->th);
    return NULL;
}

static void test_thread(void)
{
    struct ThreadTestParam param;
    param.counter = 0;
    struct Thread *th = thread_launch(thread_func, &param, "test thread %d", param.counter);
    OK_FATAL(th, "have thread object");
    unsigned id1 = thread_getid(th);
    usleep(1000*50); // is this enough?
    OK(param.counter == 1, "thread worked");
    OK(strcmp(thread_getname(th), "test thread 0") == 0, "good name");
    th = thread_stop(th);
    OK_FATAL(th == NULL, "thread object gone");

    // thread_stop() doesn't do anything to self
    param.counter = 0;
    param.th = thread_launch(thread_exit_func, &param, "test exit");
    OK_FATAL(param.th, "have thread object");
    unsigned id2 = thread_getid(param.th);
    usleep(4000); // wait for the thread
    OK(param.counter == 21, "thread_stop didn't stop itself");

    // thread_exit() doesn't do anything to others
    param.counter = 0;
    param.th = thread_launch(thread_exit_func, &param, "test exit");
    OK_FATAL(param.th, "have thread object");
    unsigned id3 = thread_getid(param.th);
    thread_exit(param.th); // should do nothing
    usleep(4000); // wait for the thread
    OK(param.counter == 21, "thread_exit didn't stop the thread");

    OK(id1 != id2, "different id");
    OK(id1 != id3, "different id");
    OK(id2 != id3, "different id");

    OK(thread_stop(NULL) == NULL, "stop NULL thread");
    thread_exit(NULL); // exit NULL thread shouldn't segfault either

    th = thread_launch_priority(thread_func, &param, 10, "test thread %s", "prio");
    if (th) {
        usleep(1000*50); // is this enough?
        OK(param.counter == 2, "priority thread worked");
        OK(strcmp(thread_getname(th), "test thread prio") == 0, "good name");
        th = thread_stop(th);
        OK_FATAL(th == NULL, "thread object gone");
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
        struct ThreadTestParam *message = (struct ThreadTestParam *)messagequeue_pop(param->q, 1000*200);
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
            usleep(1000*20);
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
    struct ThreadMQParam param;
    param.counter = 0;
    param.timeouts = 0;
    param.errors = 0;
    param.q = new_messagequeue();
    OK_FATAL(param.q, "have queue");
    struct Thread *th = thread_launch(thread_mq_timeout_func, &param, "test mq timeout");
    OK_FATAL(th, "have thread object");

    usleep(1000*300);
    OK(param.counter == 0, "counter %d", param.counter);
    OK(param.timeouts == 1, "timeouts %d", param.timeouts);

    struct ThreadTestParam *message;
    message = (struct ThreadTestParam *)malloc(sizeof(struct ThreadTestParam));
    message->counter = 4;
    messagequeue_push(param.q, message);
    message = (struct ThreadTestParam *)malloc(sizeof(struct ThreadTestParam));
    message->counter = 3;
    messagequeue_push(param.q, message);
    usleep(1000*50);
    OK(param.counter == 7, "counter %d", param.counter);
    OK(param.timeouts == 1, "timeouts %d", param.timeouts);

    usleep(1000*300);
    OK(param.counter == 7, "counter %d", param.counter);
    OK(param.timeouts == 2, "timeouts %d", param.timeouts);

    //usleep(1000*30000); // to check the thread name in htop

    th = thread_stop(th);
    OK_FATAL(th == NULL, "thread object gone");
    param.counter = 0;
    param.timeouts = 0;

    th = thread_launch(thread_mq_immediate_func, &param, "test mq immediate");
    OK_FATAL(th, "have thread object");
    usleep(1000*50);
    OK(param.counter == 0, "counter %d", param.counter);
    OK(param.timeouts == 3, "timeouts %d", param.timeouts);
    message = (struct ThreadTestParam *)malloc(sizeof(struct ThreadTestParam));
    message->counter = 9;
    messagequeue_push(param.q, message);
    usleep(1000*50);
    OK(param.counter == 9, "counter %d", param.counter);
    OK(param.timeouts == 5, "timeouts %d", param.timeouts);

    th = thread_stop(th);
    OK_FATAL(th == NULL, "thread object gone");
    param.counter = 0;
    param.timeouts = 0;

    th = thread_launch(thread_mq_notimeout_func, &param, "test mq notimeout");
    OK_FATAL(th, "have thread object");
    usleep(1000*200);
    OK(param.counter == 0, "counter %d", param.counter);
    OK(param.timeouts == 0, "timeouts %d", param.timeouts);
    message = (struct ThreadTestParam *)malloc(sizeof(struct ThreadTestParam));
    message->counter = 5;
    messagequeue_push(param.q, message);
    usleep(1000*50);
    OK(param.counter == 5, "counter %d", param.counter);
    OK(param.timeouts == 0, "timeouts %d", param.timeouts);

    th = thread_stop(th);
    OK_FATAL(th == NULL, "thread object gone");
    param.counter = 0;
    param.timeouts = 0;

    // test pushing multiple items
    for (int i=1; i<20; i++) {
        message = (struct ThreadTestParam *)malloc(sizeof(struct ThreadTestParam));
        message->counter = i;
        messagequeue_push(param.q, message);
    }
    th = thread_launch(thread_mq_multi_func, &param, "test mq multiple msg");
    OK_FATAL(th, "have thread object");
    usleep(1000*100);
    OK(param.counter == 19, "counter %d", param.counter);
    OK(param.timeouts == 0, "timeouts %d", param.timeouts);
    OK(param.errors == 0, "errors %d", param.errors);

    th = thread_stop(th);
    OK_FATAL(th == NULL, "thread object gone");

    param.q = delete_messagequeue(param.q);
    OK(param.q == NULL, "queue gone");

    OK(delete_messagequeue(NULL) == NULL, "delete NULL queue");
}

TEST_CASES = {
    {"thread", test_thread},
    {"message queue", test_mq},
    {NULL, NULL}
};

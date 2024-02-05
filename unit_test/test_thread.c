
#include "testing.h"

#include "thread_utils.h"

#include <stdlib.h>

TEST_INIT("Thread Utils");

//TODO we have to be careful, testing.h is not thread-safe!

struct ThreadTestParam {
    int counter;
};

static void *thread_func(void *arg)
{
    struct ThreadTestParam *param = arg;
    param->counter++;
    while (1) { usleep(1000*1000); }
    return NULL;
}

static void test_thread(void)
{
    struct ThreadTestParam param;
    param.counter = 0;
    struct R2Thread *th = thread_launch("test thread", thread_func, &param);
    OK_FATAL(th, "have thread object");
    usleep(1000*50); // is this enough?
    OK(param.counter == 1, "thread worked");
    th = thread_stop(th);
    OK_FATAL(th == NULL, "thread object gone");

    th = thread_launch_priority("test thread", thread_func, &param, 10);
    if (th) {
        usleep(1000*50); // is this enough?
        OK(param.counter == 2, "priority thread worked");
        th = thread_stop(th);
        OK_FATAL(th == NULL, "thread object gone");
    } else {
        SKIP("need CAP_SYS_NICE for the thread priority test");
    }
}

struct ThreadMQParam {
    int counter;
    int timeouts;
    struct MessageQueue *q;
};

static void *thread_mq_func(void *arg)
{
    struct ThreadMQParam *param = arg;

    while (1) {
        struct ThreadTestParam *message = messagequeue_pop(param->q, 1000*200);
        if (message) {
            param->counter += message->counter;
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
    param.q = new_messagequeue();
    OK_FATAL(param.q, "have queue");
    struct R2Thread *th = thread_launch("test thread", thread_mq_func, &param);
    OK_FATAL(th, "have thread object");

    usleep(1000*300);
    OK(param.counter == 0, "counter %d", param.counter);
    OK(param.timeouts == 1, "timeouts %d", param.timeouts);

    struct ThreadTestParam *message;
    message = malloc(sizeof(struct ThreadTestParam));
    message->counter = 4;
    messagequeue_push(param.q, message);
    message = malloc(sizeof(struct ThreadTestParam));
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
    param.q = delete_messagequeue(param.q);
    OK(param.q == NULL, "queue gone");
}

TEST_CASES = {
    {"thread", test_thread},
    {"message queue", test_mq},
    {NULL, NULL}
};

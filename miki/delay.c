
#define _GNU_SOURCE

#include "delay.h"
#include "utils.h"

#include <stdlib.h>
#include <stdio.h>

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>

struct DelayQueue {
    //TODO keep a list of PipelineIterator objects (priority queue)
    //      a background thread will get them from the queue and pipe_iterator_run()
};

static struct DelayQueue *delay_queue = NULL;
static pthread_t delay_tid;
static sem_t delay_sem;

static void *delay_thread(void *arg)
{
    (void)arg;
    printf("Delay thread starting\n");
    pthread_setname_np(pthread_self(), "delay thread");

    while (1) {
        sem_wait(&delay_sem);
        //TODO now we have some packets in the queue

        //TODO when we get a PipelineIterator from the queue the current action is the delay
        //      we need to step to the next action before pipe_iterator_run()
    }

    return NULL;
}

bool init_delay(void)
{
    if (sem_init(&delay_sem, 0, 0) < 0) {
        perror("init_delay sem_init");
        return false;
    }

    delay_queue = calloc_struct(DelayQueue);

    pthread_attr_t attr;
    if ((errno = pthread_attr_init(&attr)) != 0) {
        perror("delay thread pthread_attr_init");
        return false;
    }
    if ((errno = pthread_attr_setschedpolicy(&attr, SCHED_FIFO)) != 0) {
        perror("delay thread pthread_attr_setschedpolicy");
        return false;
    }
    struct sched_param param;
    param.sched_priority = 97;
    if ((errno = pthread_attr_setschedparam(&attr, &param)) != 0) {
        perror("delay thread pthread_attr_setschedparam");
        return false;
    }
    if ((errno = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED)) != 0) {
        perror("pthread_attr_setinheritsched");
        return -1;
    }

    if (pthread_create(&delay_tid, &attr, &delay_thread, NULL) != 0) {
        fprintf(stderr, "could not create delay thread\n");
        return false;
    }

    return true;
}

void fini_delay(void)
{
    pthread_cancel(delay_tid); //TODO flush the queue first
    pthread_join(delay_tid, NULL);
}

void delay_insert(struct PipelineIterator *pi, unsigned delay)
{
    (void)pi;
    (void)delay;

    //TODO insert_into_delay_queue()
    //TODO sem_post(&delay_sem);
}



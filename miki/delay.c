
#define _GNU_SOURCE

#include "delay.h"
#include "utils.h"

#include <stdlib.h>
#include <stdio.h>

#include <pthread.h>
#include <semaphore.h>

struct DelayBuffer {
    //TODO keep a list of PipelineIterator objects (priority queue)
    //      a background thread will get them from the queue and pipe_iterator_run()
};

static struct DelayBuffer *delay_buffer = NULL;
static pthread_t delay_tid;
static sem_t delay_sem;

static void *delay_thread(void *arg)
{
    (void)arg;
    printf("Delay thread starting\n");
    pthread_setname_np(pthread_self(), "delay thread");

    while (1) {
        sem_wait(&delay_sem);
        //TODO now we have some packets in the buffer
    }

    return NULL;
}

bool init_delay(void)
{
    if (sem_init(&delay_sem, 0, 0) < 0) {
        perror("init_delay sem_init");
        return false;
    }

    delay_buffer = calloc_struct(DelayBuffer);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    struct sched_param param;
    param.sched_priority = 97;
    pthread_attr_setschedparam(&attr, &param);
    if (pthread_create(&delay_tid, &attr, &delay_thread, NULL) != 0) {
        fprintf(stderr, "could not create delay thread\n");
        return false;
    }

    return true;
}

void fini_delay(void)
{
    pthread_cancel(delay_tid);
    pthread_join(delay_tid, NULL);
}

void delay_insert(struct PipelineIterator *pi, unsigned delay)
{
    (void)pi;
    (void)delay;

    //TODO insert_into_delay_buffer()
    //TODO sem_post(&delay_sem);
}



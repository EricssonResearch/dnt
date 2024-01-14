// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#define _GNU_SOURCE

#include "action.h"
#include "delay.h"
#include "log.h"
#include "pipeline.h"
#include "time_utils.h"
#include "utils.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/select.h>


//TODO: we need a thread_utils.h
//TODO: Cleanup the code, remove legacy bits
DEFAULT_LOGGING_MODULE(DELAY, WARNING)


struct DelayQueue {
    struct PipelineIterator *pi;
    unsigned delay_ms;
    struct timespec due_time;
    struct DelayQueue *next;
};

static struct DelayQueue *delay_queue = NULL;
static pthread_t delay_tid;
static sem_t delay_sem;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
struct timespec delay_timer;
int ev_fds;

static void *delay_thread(void *arg)
{
    (void)arg;
    log_info("Delay thread starting\n");
    pthread_setname_np(pthread_self(), "delay thread");

    fd_set  readfds;
    int nfds = ev_fds+1;
    struct timespec time_now, wait;

    int ret;
    uint64_t val;

    FD_ZERO(&readfds);

    // wait for the first packet to be delayed
    sem_wait(&delay_sem);


    while (1) {
        FD_SET(ev_fds, &readfds);
        clock_gettime(CLOCK_REALTIME, &time_now);
        timespecsub(&delay_timer, &time_now, &wait);

        //TODO: convert to poll API as agreed months ago...
        ret = pselect(nfds, &readfds, 0, 0, &wait, NULL);
        if(ret > 0){
            ret = read(ev_fds, &val, sizeof(val));
            //printf("event interrupted, %d\n", val);
            continue; // start over to recalculate timer
        }

        pthread_mutex_lock (&mutex);

        // delay time elapsed, send out
        struct DelayQueue* pDelayQueueFirst = delay_queue;
        if(pDelayQueueFirst == NULL){
            /* printf("ERROR: Packet delay timer expired but queue empty.\n"); */
            pthread_mutex_unlock (&mutex);
            sem_wait(&delay_sem);
            continue;
        }

        struct PipelineIterator *pi = pDelayQueueFirst->pi;
        //struct Action *a = pi->pipe->actions[pi->pos];

        // Move  to the next frame in tt queue
        delay_queue = pDelayQueueFirst->next;
        free(pDelayQueueFirst);


        // Unlock mutex
        pthread_mutex_unlock (&mutex);

        //      we need to step to the next action before pipe_iterator_run()
        pi->pos++;
        pipe_iterator_run(pi);

        // Check if the tt queue is empty
        if(delay_queue != NULL){               // more packets in queue, prime timer
            delay_timer.tv_sec = delay_queue->due_time.tv_sec;
            delay_timer.tv_nsec = delay_queue->due_time.tv_nsec;

            log_debug("* %lu.%09ld  timer %ld.%09ld", time_now.tv_sec,time_now.tv_nsec, delay_queue->due_time.tv_sec, delay_queue->due_time.tv_nsec);
            // it will sleep
        }else{
            // no follow-up packet, just wait for semaphore
            sem_wait(&delay_sem);
        }

    }

    return NULL;
}

bool init_delay(void)
{
    if (sem_init(&delay_sem, 0, 0) < 0) {
        perror("init_delay sem_init");
        return false;
    }

    //delay_queue = calloc_struct(DelayQueue);

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
        return false;
    }

    if (pthread_create(&delay_tid, &attr, &delay_thread, NULL) != 0) {
        if (pthread_create(&delay_tid, NULL, &delay_thread, NULL) != 0) {
            log_error("could not create delay thread\n");
            return false;
        }
        log_warning("could not set priority for delay thread, need CAP_SYS_NICE\n");
    }

    ev_fds = eventfd(0, EFD_NONBLOCK);
    if(ev_fds == -1){
        log_error("Create eventfd failed. \n");
        return false;
    }

    return true;
}

void fini_delay(void)
{
    pthread_cancel(delay_tid); //TODO flush the queue first
    pthread_join(delay_tid, NULL);

    close(ev_fds);
    free(delay_queue);
}

void delay_insert(struct PipelineIterator *pi, unsigned timestamp, unsigned delay)
{
    // alloc and fill in the tt_queue entry
    struct DelayQueue* pDelayQueueEntry = calloc_struct(DelayQueue);
    if(pDelayQueueEntry == NULL){
        log_warning("Insufficient memory.\n");
        // TODO handle error
        return;
    }

    pDelayQueueEntry->pi = pi;
    pDelayQueueEntry->delay_ms = delay;
    pDelayQueueEntry->next = NULL;

    // get current time
    clock_gettime(CLOCK_REALTIME, &pDelayQueueEntry->due_time);

    unsigned ts_now = (pDelayQueueEntry->due_time.tv_nsec/1000) | (pDelayQueueEntry->due_time.tv_sec & 0x00000001)<<20;

    if(ts_now<timestamp){
        // the first bit switched from 1 to 0
        pDelayQueueEntry->due_time.tv_sec -= 1;
        pDelayQueueEntry->due_time.tv_nsec = (timestamp & 0x0FFFFF)*1000;
    } else {
        // replace lower bits from tstamp
        pDelayQueueEntry->due_time.tv_sec = (pDelayQueueEntry->due_time.tv_sec & 0xFFFFFFFE) | ((timestamp >> 20) & 0x00000001);
        pDelayQueueEntry->due_time.tv_nsec = (timestamp & 0x0FFFFF)*1000;
    }
    // Add delay configured in ms to the timestamp received (we delay tt_delay_ms from tstamp received)
    // The configured ms delay value should not be > 1s
    pDelayQueueEntry->due_time.tv_nsec += delay*1000000;
    if(pDelayQueueEntry->due_time.tv_nsec >= 1000000000) {
        pDelayQueueEntry->due_time.tv_sec++;
        pDelayQueueEntry->due_time.tv_nsec -= 1000000000;
    }

    // handling the delay queue should not be interrupted
    pthread_mutex_lock (&mutex);

    // find the frame in the tt_queue where we have to insert
    struct DelayQueue *pDelayQueueIterator, *pDelayQueueIteratorPrev;

    pDelayQueueIterator = (struct DelayQueue *) delay_queue; pDelayQueueIteratorPrev = NULL;
    while(pDelayQueueIterator != NULL){
        if(timespeccmp(&pDelayQueueIterator->due_time, &pDelayQueueEntry->due_time, >))
            break;
        pDelayQueueIteratorPrev = pDelayQueueIterator;
        pDelayQueueIterator = pDelayQueueIterator->next;
    }


    uint64_t val = 1;
    if(pDelayQueueIteratorPrev == NULL){

        // if the  queue not empty, set the next
        // if(tt_queue != NULL)
        pDelayQueueEntry->next = (struct DelayQueue *) delay_queue;

        // first in the delay queue, set timer for it
        delay_timer.tv_sec = pDelayQueueEntry->due_time.tv_sec;
        delay_timer.tv_nsec = pDelayQueueEntry->due_time.tv_nsec;

        // first in the queue?
        if(delay_queue == NULL){
            delay_queue = pDelayQueueEntry;

            // unlock mutex
            pthread_mutex_unlock (&mutex);

            sem_post(&delay_sem);
        }else{
            // this should be first, interrupt waiting...
            delay_queue = pDelayQueueEntry;

            // interrupt current pselect
            if(write(ev_fds, &val, sizeof(val)) <= 0)
                printf ("write event fd error!!!\n");       // TODO: THROW

            // unlock mutex
            pthread_mutex_unlock (&mutex);
        }
    } else {  // not first, insert into the queue
        if(delay_queue == NULL)
            printf("Should not happen\n");

        // put in the queue
        pDelayQueueEntry->next = pDelayQueueIteratorPrev->next;
        pDelayQueueIteratorPrev->next = pDelayQueueEntry;
        // timer must be running, not needed to set...
        pthread_mutex_unlock (&mutex);
    }

#if defined (DEBUG_TSTAMP)
    if(due_time.tv_nsec - t2.tv_nsec < 0)
        printf(" delay: %ld.%09ld\n", due_time.tv_sec-1-t2.tv_sec, due_time.tv_nsec -t2.tv_nsec + 1000000000);
    else
        printf(" delay: %ld.%09ld\n", due_time.tv_sec-t2.tv_sec, due_time.tv_nsec -t2.tv_nsec);
#endif

}

// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.

#include "action.h"
#include "delay.h"
#include "log.h"
#include "pipeline.h"
#include "utils.h"
#include "thread_utils.h"
#include "notification.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <semaphore.h>
#include <sys/eventfd.h>
#include <sys/select.h>


DEFAULT_LOGGING_MODULE(DELAY, WARNING);

int delay_actions = 0;

struct DelayStat {
    unsigned long long delayed_packets;
    unsigned long long delay_exceeded_packets;
};

struct DelayQueue {
    struct PipelineIterator *pi;
    struct timespec delay;
    struct timespec due_time;
    struct DelayStat *stat;
    struct DelayQueue *next;
};

static struct HashMap *stats = NULL;

static struct DelayQueue *delay_queue = NULL;
static struct Thread *thread = NULL;
static sem_t delay_sem;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
struct timespec delay_timer;
struct timespec last_alert;


static NotificationLevel delay_notification_pull_fn(void *self, struct JsonValue **msg)
{
    (void)self;
    struct JsonValue *ret = json_object();

    HASHMAP_ITERATE(stats, s) {
        struct DelayStat *stat = (struct DelayStat *)hash_iterator_value(&s);
        const char *pipeline_name = hash_iterator_key(&s);
        struct JsonValue *js = json_object();
        json_object_insert(js, "delayed_packets", json_number(stat->delayed_packets));
        json_object_insert(js, "delay_exceeded_packets", json_number(stat->delay_exceeded_packets));
        json_object_insert(ret, pipeline_name, js);
    }

/*    unsigned len;
    log_info("js=%s\n", json_serialize(ret, &len));
*/
    *msg = ret;
    return NOTIF_PULL;
}

bool register_delay_notification(bool add, char *target, unsigned period_ms)
{
    if(add)
        return notification_register_source("delay", delay_notification_pull_fn, target, period_ms);
    else
        return notification_register_source("delay", NULL, target, period_ms);
}

static void *delay_thread(void *arg)
{
    (void)arg;
    log_info("Delay thread starting");

    struct timespec time_now;
    int ret;

    // wait for the first packet to be delayed
    sem_wait(&delay_sem);

    while (1) {
        clock_gettime(CLOCK_REALTIME, &time_now);

        // wait until the delay time has elapsed
        while ((ret = sem_timedwait(&delay_sem, &delay_timer)) == -1 && errno == EINTR)
            continue;

        if (ret == -1 && errno != ETIMEDOUT)
            log_perror("sem_timedwait");

        pthread_mutex_lock(&mutex);

        // delay time elapsed, send out
        struct DelayQueue *pDelayQueueFirst = delay_queue;
        if (pDelayQueueFirst == NULL) {
            /* printf("ERROR: Packet delay timer expired but queue empty.\n"); */
            pthread_mutex_unlock(&mutex);
            sem_wait(&delay_sem);
            continue;
        }

        struct PipelineIterator *pi = pDelayQueueFirst->pi;
        //struct Action *a = pi->pipe->actions[pi->pos];

        pDelayQueueFirst->stat->delayed_packets++;

        // Move to the next frame in tt queue
        delay_queue = pDelayQueueFirst->next;
        free(pDelayQueueFirst);

        // Unlock mutex
        pthread_mutex_unlock(&mutex);

        // we need to step to the next action before pipe_iterator_run()
        pi->pos++;
        pipe_iterator_run(pi);

        // Check if the tt queue is empty
        if (delay_queue != NULL) { // more packets in queue, prime timer
            delay_timer.tv_sec = delay_queue->due_time.tv_sec;
            delay_timer.tv_nsec = delay_queue->due_time.tv_nsec;

            log_debug("* %lu.%09ld  timer %ld.%09ld", time_now.tv_sec,time_now.tv_nsec, delay_queue->due_time.tv_sec, delay_queue->due_time.tv_nsec);
            // it will sleep
        } else {
            // no follow-up packet, just wait for semaphore
            sem_wait(&delay_sem);
        }
    }

    return NULL;
}

bool init_delay(void)
{
    if (sem_init(&delay_sem, 0, 0) < 0) {
        log_perror("init_delay sem_init");
        return false;
    }

    stats = new_hashmap(13, NULL, NULL);

    //delay_queue = calloc_struct(DelayQueue);
    thread = thread_launch_priority(delay_thread, NULL, 97, "delay thread");
    if (thread == NULL) {
        thread = thread_launch(delay_thread, NULL, "delay thread");
        if (thread == NULL) {
            log_error("Could not create delay thread");
            return false;
        }
        log_warning("Could not set priority for delay thread");
    }

    clock_gettime(CLOCK_REALTIME, &last_alert);

    static char name[] = "delay";  // ToDo: per pipeline delay stat?
    notification_register_source(name, delay_notification_pull_fn, name, 2000);

    return true;
}

void fini_delay(void)
{
    thread_stop(thread);
    sem_destroy(&delay_sem);
    free(delay_queue);
    delete_hashmap(stats);
}

void delay_insert(struct PipelineIterator *pi, unsigned timestamp, const struct timespec delay)
{
    // alloc and fill in the tt_queue entry
    struct DelayQueue* pDelayQueueEntry = calloc_struct(DelayQueue);
    if (pDelayQueueEntry == NULL) {
        log_warning("Insufficient memory.");
        // TODO handle error
        return;
    }

    // find the stats related to pipe
    struct DelayStat *stat = (struct DelayStat *)hashmap_find(stats, pi->pipe->name);
    if (!stat) {
        stat = calloc_struct(DelayStat);
        hashmap_insert(stats, strdup(pi->pipe->name), stat);
    }

    pDelayQueueEntry->pi = pi;
    pDelayQueueEntry->delay = delay;
    pDelayQueueEntry->stat = stat;
    pDelayQueueEntry->next = NULL;

    // get current time
    struct timespec now_ts;
    clock_gettime(CLOCK_REALTIME, &now_ts);
    timespec_from_tsntstamp(&pDelayQueueEntry->due_time, timestamp, &now_ts);

    // Add delay configured in microsec to the received timestamp
    struct timespec result;
    timespecadd(&pDelayQueueEntry->due_time, &delay, &result);
    pDelayQueueEntry->due_time = result;

    if(time_diff_us(now_ts, pDelayQueueEntry->due_time) > 0){
        stat->delay_exceeded_packets++;
        // already over due time, send notification once per sec
        if (time_diff_us(now_ts, last_alert) > 1000*1000*5) {
            log_perror("delay %s", pi->pipe->name);
/*          // push notification
            struct JsonValue *js = json_object();
            json_object_insert(js, "delay", json_string(pi->pipe->name));
            json_object_insert(js, "error", json_string("Delay already exceeded"));
            notification_push_event("delay", NOTIF_ERROR, js);
            last_alert = now_ts;
*/
        }
    }

    // handling the delay queue should not be interrupted
    pthread_mutex_lock(&mutex);

    // find the frame in the tt_queue where we have to insert
    struct DelayQueue *pDelayQueueIterator, *pDelayQueueIteratorPrev;
    pDelayQueueIterator = (struct DelayQueue *)delay_queue;
    pDelayQueueIteratorPrev = NULL;

    while (pDelayQueueIterator != NULL) {
        if (timespeccmp(&pDelayQueueIterator->due_time, &pDelayQueueEntry->due_time, >))
            break;
        pDelayQueueIteratorPrev = pDelayQueueIterator;
        pDelayQueueIterator = pDelayQueueIterator->next;
    }

    if (pDelayQueueIteratorPrev == NULL) {
        // if the  queue not empty, set the next
        // if(tt_queue != NULL)
        pDelayQueueEntry->next = (struct DelayQueue *)delay_queue;

        // first in the delay queue, set timer for it
        delay_timer.tv_sec = pDelayQueueEntry->due_time.tv_sec;
        delay_timer.tv_nsec = pDelayQueueEntry->due_time.tv_nsec;

        delay_queue = pDelayQueueEntry;

        // unlock mutex
        pthread_mutex_unlock(&mutex);

        if (sem_post(&delay_sem) == -1)
            log_error("sem_post failed");

    } else { // not first, insert into the queue
        if (delay_queue == NULL)
            log_error("Should not happen");

        // put in the queue
        pDelayQueueEntry->next = pDelayQueueIteratorPrev->next;
        pDelayQueueIteratorPrev->next = pDelayQueueEntry;
        // timer must be running, not needed to set...
        pthread_mutex_unlock(&mutex);
    }

#if defined (DEBUG_TSTAMP)
    if(due_time.tv_nsec - t2.tv_nsec < 0)
        printf(" delay: %ld.%09ld\n", due_time.tv_sec-1-t2.tv_sec, due_time.tv_nsec -t2.tv_nsec + 1000000000);
    else
        printf(" delay: %ld.%09ld\n", due_time.tv_sec-t2.tv_sec, due_time.tv_nsec -t2.tv_nsec);
#endif

}

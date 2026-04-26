// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.

#include "delay.h"
#include "action.h"
#include "log.h"
#include "notification.h"
#include "pipeline.h"
#include "thread_utils.h"
#include "utils.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <unistd.h>
#include <pthread.h>

DEFAULT_LOGGING_MODULE(DELAY, INFO);

struct DelayStat {
    uint64_t delayed_packets;
    uint64_t delay_exceeded_packets;
};

struct DelayQueue {
    struct PipelineIterator *pi;
    struct timespec deadline;
    struct DelayStat *stat;
    struct DelayQueue *next;
};

static struct HashMap *stats = NULL; // pipeline name -> struct DelayStat

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

struct MessageQueue *mbox = NULL;

#define IDLE_TIMEOUT_US 60*1000*1000

#define ALERT_COOLDOWN 5000000

// this is an unique non-NULL pointer
#define FLUSH_SIGNAL (struct PipelineIterator*)&mbox

// make sure the read of @x is not optimized away
// idea stolen from Linux kernel
#define READ_ONCE(x) (* (const volatile typeof(x) *) &(x))

// @returns the first deadline
// can be negative if we are late
static int queue_deadline(struct DelayQueue *q)
{
    struct timespec deadline = q->deadline;
    struct timespec now;
    clock_gettime(DELAY_CLOCK, &now);
    return time_diff_us(deadline, now);
}

// @returns the new deadline
static int queue_push(struct DelayQueue **q, struct PipelineIterator *pi)
{
    struct DelayQueue *n = calloc_struct(DelayQueue);
    n->pi = pi;

    struct timespec now;
    clock_gettime(DELAY_CLOCK, &now);
    struct timespec packet_enter;
    timespec_from_tsntstamp(&packet_enter, pi->packet->timestamp, &now);
    timespecadd(&packet_enter, &pi->packet->delay, &n->deadline);

    struct DelayStat *stat = (struct DelayStat *)hashmap_find(stats, pi->pipe->name);
    if (!stat) {
        stat = calloc_struct(DelayStat);
        hashmap_insert(stats, strdup(pi->pipe->name), stat);
    }
    n->stat = stat;

    // push in, increasing order of deadlines
    if (*q) {
        struct DelayQueue *iter = *q;
        struct DelayQueue *iter_prev = NULL;
        while (iter && timespeccmp(&n->deadline, &iter->deadline, >)) {
            iter_prev = iter;
            iter = iter->next;
        }
        if (iter_prev) {
            iter_prev->next = n;
            n->next = iter;
        } else {
            n->next = *q;
            *q = n;
        }
    } else {
        *q = n;
    }

    struct timespec deadline = (*q)->deadline;
    int64_t timeout = time_diff_us(deadline, now);
    if (timeout <= 0) {
        (*q)->stat->delay_exceeded_packets++;
    }
    return timeout;
}

// @returns true if there was a packet in &q
static bool queue_send_first(struct DelayQueue **q)
{
    if (*q) {
        struct DelayQueue *f = *q;
        struct PipelineIterator *pi = f->pi;

        pipe_iterator_resume(pi);

        f->stat->delayed_packets++;

        *q = (*q)->next;
        free(f);
        return true;
    } else {
        return false;
    }
}

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

    *msg = ret;
    return NOTIF_PULL;
}

static void send_alert(struct PipelineIterator *pi, struct DelayStat *stat)
{
    log_warning("delay exceeded for pipe %s (%lu of %lu)",
            pi->pipe->name, stat->delay_exceeded_packets, stat->delayed_packets);
    struct JsonValue *js = json_object();
    json_object_insert(js, "pipe", json_string(pi->pipe->name));
    json_object_insert(js, "exceeded", json_number(stat->delay_exceeded_packets));
    notification_push_event("delay", NOTIF_ERROR, js);
}

struct ThreadArgs {
    struct MessageQueue *m;
    struct Thread *t;
};
static void *delay_thread(void *arg)
{
    struct ThreadArgs *targ = (struct ThreadArgs *)arg;
    int timeout_us = IDLE_TIMEOUT_US;
    struct DelayQueue *queue = NULL;
    struct timespec last_alert = {0, 0};
    log_info("delay queue started");

    while (1) {
        struct PipelineIterator *pi = NULL;
        log_debug("waiting for %d", timeout_us);
        // skip mailbox if we are running late
        if (timeout_us > 0)
            pi = (struct PipelineIterator *)messagequeue_pop(targ->m, timeout_us);

        if (pi) {
            if (pi == FLUSH_SIGNAL) {
                // send everything immediately and exit
                while (queue) {
                    queue_send_first(&queue);
                }
                log_debug("flushed");
                pthread_mutex_unlock(&mutex);
                break;
            } else {
                timeout_us = queue_push(&queue, pi);
                if (timeout_us <= 0) {
                    struct timespec now;
                    clock_gettime(DELAY_CLOCK, &now);
                    if (time_diff_us(now, last_alert) > ALERT_COOLDOWN) {
                        send_alert(queue->pi, queue->stat);
                        last_alert = now;
                    }
                }
            }
        } else {
            // timeout
            if (queue_send_first(&queue)) {
                if (queue) {
                    timeout_us = queue_deadline(queue);
                } else {
                    log_debug("going idle");
                    timeout_us = IDLE_TIMEOUT_US;
                }
            } else {
                // queue is empty so we were in idle timeout
                pthread_mutex_lock(&mutex);
                if (mbox == targ->m) {
                    mbox = NULL;
                    pthread_mutex_unlock(&mutex);

                    usleep(10*1000);
                    pi = (struct PipelineIterator *)messagequeue_pop(targ->m, 0);
                    if (pi) {
                        // somebody pushed while we were sleeping
                        // (they still had the mbox pointer)
                        // extremely unlikely event, but let's not lose the packet
                        timeout_us = queue_push(&queue, pi);
                        if (timeout_us <= 0) {
                            struct timespec now;
                            clock_gettime(DELAY_CLOCK, &now);
                            if (time_diff_us(now, last_alert) > ALERT_COOLDOWN) {
                                send_alert(queue->pi, queue->stat);
                                last_alert = now;
                            }
                        }
                    } else {
                        break;
                    }
                } else {
                    // we are not the currently active queue, it's safe to exit
                    pthread_mutex_unlock(&mutex);
                    break;
                }
            }
        }
    }

    struct Thread *t = targ->t;
    delete_messagequeue(targ->m);
    free(arg);
    log_info("delay queue stopped");
    thread_exit(t);
    return NULL;
}

static struct MessageQueue *get_mbox(void)
{
    // no mutex in the regular code path
    struct MessageQueue *m = READ_ONCE(mbox);
    if (m) {
        return m;
    } else {
        pthread_mutex_lock(&mutex);
        // must read again because another thread might have created the mbox
        m = READ_ONCE(mbox);
        if (m) {
            pthread_mutex_unlock(&mutex);
            return m;
        } else {
            m = new_messagequeue();
            struct ThreadArgs *targ = calloc_struct(ThreadArgs);
            targ->m = m;
            targ->t = thread_launch_priority(delay_thread, targ, 97, "delay thread");
            if (targ->t == NULL) {
                targ->t = thread_launch(delay_thread, targ, "delay thread");
            }
            mbox = m;
            pthread_mutex_unlock(&mutex);
            return m;
        }
    }
}

bool init_delay(void)
{
    stats = new_hashmap(13, NULL, NULL);
    notification_register_source("delay", delay_notification_pull_fn, NULL, 2000);
    log_info("delay initialized");
    return true;
}

void finish_delay(void)
{
    struct MessageQueue *m = READ_ONCE(mbox);
    // use the mutex to wait for the thread to flush its queue
    pthread_mutex_lock(&mutex);
    if (m)
        messagequeue_push(m, FLUSH_SIGNAL);
    pthread_mutex_lock(&mutex);
    mbox = NULL;
    notification_register_source("delay", NULL, NULL, 2000);
    stats = delete_hashmap(stats);
    pthread_mutex_unlock(&mutex);
    log_info("delay finished");
}


void delay_insert(struct PipelineIterator *pi)
{
    struct MessageQueue *m = get_mbox();
    messagequeue_push(m, pi);
}

bool register_delay_notification(bool add, unsigned period_ms)
{
    return notification_register_source("delay", add ? delay_notification_pull_fn : NULL, NULL, period_ms);
}

struct ForeachStatState {
    int (*cb)(const char *pipe, uint64_t packets, uint64_t delay_exceeded, void *userdata);
    void *userdata;
};
static int foreach_stat_cb(const char *key, void *value, void *userdata)
{
    struct ForeachStatState *st = (struct ForeachStatState *)userdata;
    struct DelayStat *stat = (struct DelayStat *)value;
    return st->cb(key, stat->delayed_packets, stat->delay_exceeded_packets, st->userdata);
}
int foreach_delay_stat(int (*cb)(const char *pipe, uint64_t packets, uint64_t delay_exceeded, void *userdata), void *userdata)
{
    struct ForeachStatState st = {cb, userdata};
    return hashmap_foreach(stats, foreach_stat_cb, &st);
}


// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.

#ifndef _GNU_SOURCE /* stupid g++ implicitly defines this */
#define _GNU_SOURCE /* for pthread_setname_np */
#endif

#include "thread_utils.h"
#include "log.h"
#include "time_utils.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>

#include <pthread.h>
#include <semaphore.h>

DEFAULT_LOGGING_MODULE(THREAD, WARNING);


struct Thread {
    pthread_t tid;
    char *name;
    unsigned id; // pthread_t is unusable as a stable, unique identifier
};

static unsigned next_id = 1;

static struct Thread *launch_with_attr(const char *name, void* (*thread_fn)(void *), void *thread_arg, pthread_attr_t *attr)
{
    struct Thread *ret = calloc_struct(Thread);
    ret->name = strdup(name);
    ret->id = __atomic_fetch_add(&next_id, 1, __ATOMIC_RELAXED);

    errno = pthread_create(&ret->tid, attr, thread_fn, thread_arg);
    if (errno != 0) {
        log_perror("failed to start thread %s", name);
        free(ret->name);
        free(ret);
        return NULL;
    }
    pthread_setname_np(ret->tid, name);

    log_debug("started thread %s", name);
    return ret;
}

struct Thread *thread_launch(void* (*thread_fn)(void *), void *thread_arg, const char *name, ...)
{
    char thname[16];
    va_list args;
    va_start(args, name);
    vsnprintf(thname, sizeof(thname), name, args);
    va_end(args);

    return launch_with_attr(thname, thread_fn, thread_arg, NULL);
}

struct Thread *thread_launch_priority(void* (*thread_fn)(void *), void *thread_arg, int priority, const char *name, ...)
{
    pthread_attr_t attr;
    if ((errno = pthread_attr_init(&attr)) != 0) {
        log_perror("thread_launch_priority(%s) pthread_attr_init", name);
        return NULL;
    }
    if ((errno = pthread_attr_setschedpolicy(&attr, SCHED_FIFO)) != 0) {
        log_perror("thread_launch_priority(%s) pthread_attr_setschedpolicy", name);
        return NULL;
    }
    struct sched_param param;
    param.sched_priority = priority;
    if ((errno = pthread_attr_setschedparam(&attr, &param)) != 0) {
        log_perror("thread_launch_priority(%s) pthread_attr_setschedparam", name);
        return NULL;
    }
    if ((errno = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED)) != 0) {
        log_perror("thread_launch_priority(%s) pthread_attr_setinheritsched", name);
        return NULL;
    }

    char thname[16];
    va_list args;
    va_start(args, name);
    vsnprintf(thname, sizeof(thname), name, args);
    va_end(args);

    return launch_with_attr(thname, thread_fn, thread_arg, &attr);
}

struct Thread *thread_stop(struct Thread *thread)
{
    if (thread == NULL) return NULL;
    if (thread->tid == pthread_self()) return NULL;
    log_debug("stopping thread %s", thread->name);
    pthread_cancel(thread->tid);
    pthread_join(thread->tid, NULL);
    free(thread->name);
    free(thread);
    return NULL;
}

void thread_exit(struct Thread *thread)
{
    if (thread == NULL) return;
    if (thread->tid != pthread_self()) return;
    log_debug("thread %s exiting", thread->name);
    pthread_t tid = thread->tid;
    free(thread->name);
    free(thread);
    pthread_detach(tid);
    pthread_cancel(tid);
}

const char *thread_getname(const struct Thread *thread)
{
    return thread->name;
}

unsigned thread_getid(const struct Thread *thread)
{
    return thread->id;
}

struct Message {
    void *data;
    struct Message *next;
};

struct MessageQueue {
    struct Message *queue;
    pthread_mutex_t mutex;
    sem_t semaphore; // this essentially tracks the item count in @queue
};

static void tq_push(struct Message **queue, void *data)
{
    struct Message *newm = calloc_struct(Message);
    newm->data = data;

    if (*queue) {
        struct Message *p = *queue;
        while (p->next)
            p = p->next;
        p->next = newm;
    } else {
        *queue = newm;
    }
}

static struct Message *tq_pop(struct Message **queue)
{
    struct Message *ret = *queue;
    if (ret) {
        *queue = ret->next;
        ret->next = NULL;
        return ret;
    } else {
        return NULL;
    }
}

static void *tq_pop_data(struct MessageQueue *q, bool must_be_nonempty)
{
    pthread_mutex_lock(&q->mutex);
    struct Message *m = tq_pop(&q->queue);
    pthread_mutex_unlock(&q->mutex);

    if (m == NULL) {
        if (must_be_nonempty) {
            log_error("messagequeue had nonzero item count while being empty");
        }
        return NULL;
    } else {
        void *ret = m->data;
        free(m);
        return ret;
    }
}

struct MessageQueue *new_messagequeue(void)
{
    struct MessageQueue *ret = calloc_struct(MessageQueue);
    pthread_mutex_init(&ret->mutex, NULL);
    sem_init(&ret->semaphore, 0, 0);
    return ret;
}

struct MessageQueue *delete_messagequeue(struct MessageQueue *q)
{
    if (q == NULL) return NULL;
    pthread_mutex_destroy(&q->mutex);
    sem_destroy(&q->semaphore);
    free(q);
    return NULL;
}

void messagequeue_push(struct MessageQueue *q, void *message)
{
    pthread_mutex_lock(&q->mutex);
    tq_push(&q->queue, message);
    pthread_mutex_unlock(&q->mutex);
    sem_post(&q->semaphore);
}

void *messagequeue_pop(struct MessageQueue *q, int usec)
{
    if (usec == 0) {
        // must return immediately
        if (sem_trywait(&q->semaphore) == 0) {
            return tq_pop_data(q, true);
        } else {
            if (q->queue != NULL) {
                log_error("messagequeue had zero item count while containing items");
            }
            return NULL;
        }
    }

    if (usec > 0) {
        // wait until usec has elapsed
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        struct timespec inc = {.tv_sec = usec / 1000000, .tv_nsec = (usec % 1000000)*1000};
        struct timespec end;
        timespecadd(&now, &inc, &end);
        sem_timedwait(&q->semaphore, &end);
        //TODO we may have woken up earlier due to a signal
        return tq_pop_data(q, false);
    } else { // usec < 0
        // wait indefinitely
        sem_wait(&q->semaphore);
        return tq_pop_data(q, true);
    }
}

int messagequeue_foreach(struct MessageQueue *q, int (*cb)(const void *item, void *userdata), void *userdata)
{
    pthread_mutex_lock(&q->mutex);
    struct Message *p = q->queue;
    while (p) {
        if (!cb(p->data, userdata)) {
            pthread_mutex_unlock(&q->mutex);
            return 0;
        }
        p = p->next;
    }
    pthread_mutex_unlock(&q->mutex);
    return 1;
}


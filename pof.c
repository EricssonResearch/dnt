#define _GNU_SOURCE
#include <time.h>
#include <pthread.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "pipeline.h"
#include "packet.h"
#include "utils.h"
#include "pof.h"

#define NSEC_PER_SEC    1000000000L

enum PofEvent {
    POF_IN_ORDER_PKT = 1,
    POF_OUT_OF_ORDER_PKT = 2,
    POF_TIMEOUT = 4
};

struct PofElem {
    struct Pof *pof;
    struct PipelineIterator *pi;
    struct PofElem *next;
    int seq; //for easy access
};

struct Pof {
    int pof_max_delay;
    int pof_take_any_time;
    int queue_max_len;

    int queue_len;
    int pof_last_sent;
    bool take_any;

    pthread_t thread_id;
    pthread_mutex_t lock;
    int evfd;
    // Conditional Delay Buffer implemented as ordered queue
    struct PofElem *q_head;
};

static void *pof_thread(void *);
static void pof_reset(struct Pof *pof);

struct Pof *new_pof(unsigned pof_max_delay, unsigned pof_take_any_time, unsigned queue_max_len)
{
    struct Pof *ret = calloc(1, sizeof(*ret));
    if (ret == NULL) {
        perror("calloc");
        goto err_calloc;
    }
    ret->pof_max_delay = pof_max_delay;
    ret->pof_take_any_time = pof_take_any_time;
    ret->queue_max_len = queue_max_len;
    ret->queue_len = 0;
    ret->evfd = eventfd(0, 0);
    if (ret->evfd < 0) {
        perror("eventfd");
        goto err_evfd;
    }
    if (pthread_mutex_init(&ret->lock, NULL)) {
        perror("pthread_mutex_init");
        goto err_thread;
    }
    if (pthread_create(&ret->thread_id, NULL, pof_thread, ret) != 0) {
        perror("pthread_create");
        goto err_thread;
    }

err_thread:
    close(ret->evfd);
err_evfd:
    free(ret);
err_calloc:
    return NULL;
}

struct Pof *delete_pof(struct Pof *pof)
{
    pthread_cancel(pof->thread_id);
    pthread_mutex_destroy(&pof->lock);
    pof_reset(pof);
    free(pof);
    return NULL;
}

static struct PofElem *new_pof_elem(struct Pof *pof, struct PipelineIterator *pi)
{
    struct PofElem *ret = calloc_struct(PofElem);
    ret->pof = pof;
    ret->pi = pi;
    ret->seq = pi->packet->sequence;
    return ret;
}

bool pof_insert(struct Pof *pof, struct PipelineIterator *pi)
{
    pthread_mutex_lock(&pof->lock);

    if (pof->queue_len >= pof->queue_max_len) {
        fprintf(stderr, "POF buffer is full, drop new packet.\n");
        return false;
    }

    struct PofElem *pe = new_pof_elem(pof, pi);
    struct PofElem *iter = pof->q_head;
    struct PofElem *iter_prev = NULL;
    while (iter != NULL && iter->seq < pe->seq) {
        iter_prev = iter;
        iter = iter->next;
    }
    // new elem pe is the first elem (empty queue)
    if (iter_prev == NULL) {
        pe->next = pof->q_head;
        pof->q_head = pe;
    } else { // new elem will be mid or end of list
        iter_prev->next = pe;
        pe->next = iter;
    }

    pof->queue_len += 1;
    pthread_mutex_unlock(&pof->lock); // TODO: check if OK

    unsigned long val;
    if (pe->seq <= pof->pof_last_sent + 1 || pof->take_any == true) {
        val = POF_IN_ORDER_PKT;
    } else {
        val = POF_OUT_OF_ORDER_PKT;
    }
    if (write(pof->evfd, &val, sizeof(val)) != sizeof(val)) {
        // TODO: might be fatal, terminate r2dtwo
        perror("write");
    }
    return true;
}

static void pof_reset(struct Pof *pof)
{
    struct PofElem *iter = pof->q_head;
    while (iter != NULL) {
        struct PofElem *next = iter->next;
        free(iter);
        iter = next;
    }
    pof->queue_len = 0;
    pof->q_head = NULL;
    pof->take_any = true;
}

static void pof_try_forward(struct Pof *pof, int event)
{
    (void) event;
    while (pof->queue_len > 0) {

    }
}

static void *pof_thread(void *arg)
{
    struct Pof *pof = arg;
    struct pollfd fd = { .fd = pof->evfd, .events = POLLIN };
    struct timespec now, timeout, take_any_time;
    // pof_take_any_time is in millisec
    take_any_time.tv_sec = pof->pof_take_any_time / 1000;
    take_any_time.tv_nsec = (pof->pof_take_any_time % 1000) * (NSEC_PER_SEC / 1000);

    pof_reset(pof);
    clock_gettime(CLOCK_REALTIME, &now);
    timespec_add(&timeout, &now, &take_any_time);
    struct timespec *timeout_ptr = NULL;
    while (true) {
        int ret = ppoll(&fd, 1, timeout_ptr, NULL);
        if (ret < 0) {
            perror("ppoll");
            continue;
        }
        pthread_mutex_lock(&pof->lock);
        if (ret & POLLIN) {
            int event;
            read(pof->evfd, &event, sizeof(event));
            if (event & POF_IN_ORDER_PKT) {
                pof_try_forward(pof, event);
            }
            clock_gettime(CLOCK_REALTIME, &now);
            timespec_add(&timeout, &now, &take_any_time);
            timeout_ptr = &timeout;
        } else if (ret == 0) { // POF timeout
            pof_try_forward(pof, POF_TIMEOUT);
            pof_reset(pof);
            timeout_ptr = NULL;
        }
        pthread_mutex_unlock(&pof->lock);
    }

    return NULL;
}

#define _GNU_SOURCE
#include <stdbool.h>
#include <time.h>
#include <pthread.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "pipeline.h"
#include "packet.h"
#include "utils.h"
#include "pof.h"

enum PofEvent {
    POF_IN_ORDER_PKT = 1,
    POF_OUT_OF_ORDER_PKT = 2,
    POF_TIMEOUT = 4
};

struct PofElem {
    /* struct Pof *pof; */
    struct PipelineIterator *pi;
    struct PofElem *next;
    int seq; //for easy access
    // Absoule timepoint until the POF cond. delay buffer can hold the packet
    struct timespec forward_time;
};

struct Pof {
    struct timespec pof_max_delay;
    struct timespec pof_take_any_time;
    int queue_max_len;

    int queue_len;
    int pof_last_sent;
    bool take_any;

    pthread_t thread_id;
    pthread_mutex_t lock;
    int evfd;
    // Conditional Delay Buffer implemented as ordered queue
    struct PofElem *q_head;
    struct PofElem *next_to_forward;
    struct timespec pof_last_recv_ts;
};

static void *pof_thread(void *);
static void pof_reset(struct Pof *pof);

static void pof_debug(const struct Pof *pof)
{
    if (pof->queue_len < 2) {
        return;
    }
    struct PofElem *iter = pof->q_head;
    printf("POF: len=%d queue=", pof->queue_len);
    while (iter != NULL) {
        printf("%d ", iter->seq);
        iter = iter->next;
    }
    printf("\n");
}

static void pof_sane(const struct Pof *pof)
{
    if (pof->queue_len == 0 && pof->q_head != NULL) {
        printf("\n\n\n**********POF INTERNAL INCONSISTENCY ERROR 1!***********\n\n\n");
    }
    if (pof->q_head && pof->q_head->next == NULL && pof->queue_len != 1) {
        printf("\n\n\n**********POF INTERNAL INCONSISTENCY ERROR 2!***********\n\n\n");
    }
}

struct Pof *new_pof(unsigned pof_max_delay, unsigned pof_take_any_time, unsigned queue_max_len)
{
    struct Pof *ret = calloc(1, sizeof(*ret));
    if (ret == NULL) {
        perror("calloc");
        goto err_calloc;
    }
    timespec_from_u64(&ret->pof_max_delay, (uint64_t) pof_max_delay * NSEC_PER_SEC / 1000);
    timespec_from_u64(&ret->pof_take_any_time, (uint64_t) pof_take_any_time * NSEC_PER_SEC / 1000);
    ret->queue_max_len = queue_max_len;
    ret->queue_len = 0;
    ret->evfd = eventfd(0, EFD_NONBLOCK);
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
    return ret;

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
    /* ret->pof = pof; */
    ret->pi = pi;
    ret->seq = ntohl(pi->packet->sequence) & 0xffff;
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    timespec_add(&ret->forward_time, &now, &pof->pof_max_delay);
    return ret;
}

bool pof_insert(struct Pof *pof, struct PipelineIterator *pi)
{
    if (pof->queue_len >= pof->queue_max_len) {
        fprintf(stderr, "POF buffer is full, drop new packet.\n");
        return false;
    }

    pthread_mutex_lock(&pof->lock);
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
    clock_gettime(CLOCK_REALTIME, &pof->pof_last_recv_ts);

    unsigned long event;
    if (pe->seq <= pof->pof_last_sent + 1 || pof->take_any == true) {
        event = POF_IN_ORDER_PKT;
    } else {
        event = POF_OUT_OF_ORDER_PKT;
    }
    if (write(pof->evfd, &event, sizeof(event)) != sizeof(event)) {
        // TODO: might be fatal, terminate r2dtwo
        perror("write");
    }
    pthread_mutex_unlock(&pof->lock); // TODO: check if OK
    return true;
}

static void pof_pop_item(struct Pof *pof, struct PofElem *pe)
{
    if (pe == NULL)
        return;

    /* struct Pof *pof = pe->pof; */
    pof_sane(pof);

    if(pof->queue_len) {
        struct PofElem *iter, *iter_prev;
        iter = pof->q_head;
        iter_prev = NULL;
        while (iter != NULL && iter != pe) {
            iter_prev = iter;
            iter = iter->next;
        }
        printf("POF: iterprev=%p iter=%p pe=%p\n", iter_prev, iter, pe);
        if (iter_prev == NULL) {
            pof->q_head = iter->next;
        } else {
            iter_prev->next = iter->next;
        }
        printf("POF DELETE=%p\n", pe);
        free(pe);
        pof->queue_len -= 1;
    }
}

static void pof_reset(struct Pof *pof)
{
    pof_sane(pof);
    while (pof->q_head)
        pof_pop_item(pof, pof->q_head);
    pof->take_any = true;
    printf("\n\n\n\n\nPOF reset\n\n\n\n\n\n");
}

static struct timespec *get_next_deadline(struct Pof *pof)
{
    pof_sane(pof);
    pof->next_to_forward = NULL;
    if (pof->queue_len == 0)
        return NULL;
    struct timespec *ret = &pof->q_head->forward_time;
    struct PofElem *iter = pof->q_head;
    while (iter) {
        if (timespec_compare(&iter->forward_time, ret)) {
            ret = &iter->forward_time;
            pof->next_to_forward = iter;
        }
        iter = iter->next;
    }
    return ret;
}

static void pof_forward(struct Pof *pof, struct PofElem *pe)
{
    /* struct Pof *pof = pe->pof; */
    pof->pof_last_sent = pe->seq;
    printf("POF forward seq=%d address=%p\n", pe->seq, pe->pi->packet);
    pe->pi->pos += 1; // advance in the pipeline
    pipe_iterator_run(pe->pi);
}

static void pof_try_forward(struct Pof *pof, int event)
{
    pof_sane(pof);
    struct PofElem *pkt_to_send = pof->q_head;
    if (event & POF_TIMEOUT && pof->take_any == false) {
        if (pof->next_to_forward)
            pkt_to_send = pof->next_to_forward;
    }
    while (pof->queue_len > 0) {
        if (pkt_to_send->seq == pof->pof_last_sent + 1 || (event & POF_TIMEOUT)) {
            if (pof->take_any)
               pof->take_any = false;
            pof_forward(pof, pkt_to_send);
            if (event & POF_TIMEOUT) {
                event &= ~POF_TIMEOUT;
            }
            pof_pop_item(pof, pkt_to_send);
        }
        else if (pkt_to_send->seq < pof->pof_last_sent + 1){
            pof_forward(pof, pkt_to_send);
            pof_pop_item(pof, pkt_to_send);
        } else
            break;

        pkt_to_send = pof->q_head;
    }
}

static void *pof_thread(void *arg)
{
    struct Pof *pof = arg;
    struct pollfd fd = { .fd = pof->evfd, .events = POLLIN };
    pthread_setname_np(pthread_self(), "pof thread");
    pof_reset(pof);

    struct timespec now, timeout;
    struct timespec *next_deadline = get_next_deadline(pof);
    clock_gettime(CLOCK_REALTIME, &now);
    if (next_deadline && timespec_compare(next_deadline, &now) > 0)
        timespec_sub(&timeout, next_deadline, &now);
    else
        timeout = pof->pof_take_any_time;
    while (true) {
        pof_sane(pof);
        int ret = ppoll(&fd, 1, &timeout, NULL);
        if (ret < 0) {
            perror("ppoll");
            continue;
        }
        pof_debug(pof);
        pthread_mutex_lock(&pof->lock);
        if (ret & POLLIN) {
            unsigned long event;
            ret = read(pof->evfd, &event, sizeof(event));
            if (ret < 0) {
                perror("read");
                goto out;
            }
            if (event & POF_IN_ORDER_PKT) {
                pof_try_forward(pof, event);
            }
        } else if (ret == 0) { // POF timeout
            if (pof->queue_len != 0) {
                pof_try_forward(pof, POF_TIMEOUT);
                goto out;
            } else
                pof_reset(pof);
            /* printf("Timeout: %lu.%lu\n", timeout.tv_sec, timeout.tv_nsec); */
        } else {
            printf("FATAL\n\n*************************************");
            printf("FATAL\n");
        }
out:
        next_deadline = get_next_deadline(pof);
        clock_gettime(CLOCK_REALTIME, &now);
        if (next_deadline && timespec_compare(next_deadline, &now) > 0)
            timespec_sub(&timeout, next_deadline, &now);
        else
            timeout = pof->pof_take_any_time;
        pthread_mutex_unlock(&pof->lock);
    }

    return NULL;
}

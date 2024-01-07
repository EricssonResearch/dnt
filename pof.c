// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.

#define _GNU_SOURCE /* for ppoll, pthread_setname_np */

#include "pof.h"
#include "log.h"
#include "object.h"
#include "packet.h"
#include "pipeline.h"
#include "time_utils.h"
#include "utils.h"

#include <stdbool.h>
#include <time.h>
#include <pthread.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <arpa/inet.h>

DEFAULT_LOGGING_MODULE(MAIN, WARNING)
LOGGING_MODULE(OAM, WARNING)

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
    // Absoule timepoint until the POF cond. delay buffer can hold the packet
    struct timespec forward_time;
};

struct Pof {
    struct PipelineObject base;

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

static inline __attribute__((unused)) void pof_debug(const struct Pof *pof)
{
    if (pof->queue_len < 0) {
        return;
    }
    struct PofElem *iter = pof->q_head;
    log_debug("POF: len=%d queue=", pof->queue_len);
    while (iter != NULL) {
        log_debug("%d ", iter->seq);
        iter = iter->next;
    }
    log_debug("\n");
}

static struct JsonValue *get_state_json(const struct PipelineObject *obj)
{
    const struct Pof *pof = (const struct Pof *)obj;
    struct JsonValue *js = json_object();
    json_object_insert(js, "type", json_string("pof"));
    json_object_insert(js, "name", json_string(obj->name));
    json_object_insert(js, "max_buffer_length", json_number((double) pof->queue_max_len));
    double max_delay = pof->pof_max_delay.tv_sec * NSEC_PER_SEC + pof->pof_max_delay.tv_nsec;
    max_delay = (max_delay / NSEC_PER_SEC) * 1000; // millisec
    json_object_insert(js, "max_delay", json_number(max_delay));
    json_object_insert(js, "last_sent", json_number((double) pof->pof_last_sent));
    json_object_insert(js, "current_buffer_length", json_number((double) pof->queue_len));
    double take_any_time = pof->pof_take_any_time.tv_sec * NSEC_PER_SEC + pof->pof_take_any_time.tv_nsec;
    take_any_time = (take_any_time / NSEC_PER_SEC) * 1000; // millisec
    json_object_insert(js, "take_any_time", json_number(take_any_time));
    return js;
}

char *pof_sprintf_state_json(struct JsonValue *json, const char *record_sep, const char *line_sep)
{
    struct JsonValue *max_delay = json_object_get_number(json, "max_delay");
    struct JsonValue *take_any_time = json_object_get_number(json, "take_any_time");
    struct JsonValue *max_buffer_length = json_object_get_number(json, "max_buffer_length");
    struct JsonValue *current_buffer_length = json_object_get_number(json, "current_buffer_length");
    struct JsonValue *last_sent = json_object_get_number(json, "last_sent");

    if (max_delay && take_any_time && max_buffer_length && current_buffer_length && last_sent) {
        return strdup_printf("max_delay %.0fms%stake_any_time%.0fms%s"
                "max_buffer_length %.0f%scurrent_buffer_length %.0f%slast_sent %.0f",
                max_delay->v.number, record_sep, take_any_time->v.number, line_sep,
                max_buffer_length->v.number, record_sep, current_buffer_length->v.number, record_sep, last_sent->v.number);
    } else {
        return strdup("<invalid pof state>");
    }
}

struct PipelineObject *new_pof(const char *name, unsigned pof_max_delay, unsigned pof_take_any_time, unsigned queue_max_len)
{
    struct Pof *ret = calloc_struct(Pof);
    if (ret == NULL) {
        perror("calloc");
        return NULL;
    }
    ret->base.type = PO_POF;
    ret->base.name = strdup(name);
    ret->base.get_state = get_state_json;

    timespec_from_msec(&ret->pof_max_delay, pof_max_delay);
    timespec_from_msec(&ret->pof_take_any_time, pof_take_any_time);
    ret->queue_max_len = queue_max_len;
    ret->queue_len = 0;
    ret->take_any = true;
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
    return (struct PipelineObject*)ret;

err_thread:
    close(ret->evfd);
err_evfd:
    free(ret);
    return NULL;
}

struct PipelineObject *delete_pof(struct PipelineObject *p)
{
    struct Pof *pof = (struct Pof*)p;
    pthread_cancel(pof->thread_id);
    pthread_join(pof->thread_id, NULL);
    pthread_mutex_destroy(&pof->lock);
    pof_reset(pof);
    free(p->name);
    free(p);
    return NULL;
}

static struct PofElem *new_pof_elem(struct Pof *pof, struct PipelineIterator *pi)
{
    struct PofElem *ret = calloc_struct(PofElem);
    ret->pof = pof;
    ret->pi = pi;
    ret->seq = ntohl(pi->packet->sequence) & 0xffff;
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    timespecadd(&now, &pof->pof_max_delay, &ret->forward_time);
    return ret;
}

bool pof_insert(struct PipelineObject *p, struct PipelineIterator *pi)
{
    struct Pof *pof = (struct Pof*)p;
    if (pof->queue_len >= pof->queue_max_len) {
        log_warning_m(OAM, "POF buffer is full, drop new packet.\n");
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
    if ((pe->seq <= pof->pof_last_sent + 1) || pof->take_any == true) {
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

static void pof_pop_item(struct PofElem *pe)
{
    if (pe == NULL || pe->pof->q_head == NULL)
        return;

    struct Pof *pof = pe->pof;

    if(pof->queue_len) {
        struct PofElem *iter, *iter_prev;
        iter = pof->q_head;
        iter_prev = NULL;
        while (iter != NULL && iter != pe) {
            iter_prev = iter;
            iter = iter->next;
        }
        if (iter_prev == NULL) {
            pof->q_head = iter->next;
        } else if(iter) {
            iter_prev->next = iter->next;
        }
        free(pe);
        pof->queue_len -= 1;
    }
}

static void pof_reset(struct Pof *pof)
{
    while (pof->q_head)
        pof_pop_item(pof->q_head);
    if (pof->take_any == false) {
        log_info("POF reset\n");
        pof->take_any = true;
    }
}

static struct timespec *get_next_deadline(struct Pof *pof)
{
    pof->next_to_forward = NULL;
    if (pof->queue_len == 0)
        return NULL;
    struct timespec *ret = &pof->q_head->forward_time;
    struct PofElem *iter = pof->q_head;
    while (iter) {
        if (timespeccmp(&iter->forward_time, ret, !=)) {
            ret = &iter->forward_time;
            pof->next_to_forward = iter;
        }
        iter = iter->next;
    }
    return ret;
}

static void pof_forward(struct PofElem *pe)
{
    struct Pof *pof = pe->pof;
    pof->pof_last_sent = pe->seq;
    pe->pi->pos += 1; // advance in the pipeline
    /* printf("POF: last_sent=%d forward=%d take_any=%d\n", pof->pof_last_sent, pe->seq, pof->take_any); */
    pipe_iterator_run(pe->pi);
}

static void pof_try_forward(struct Pof *pof, int event)
{
    struct PofElem *pkt_to_send = pof->q_head;
    if ((event & POF_TIMEOUT) && pof->take_any == false) {
        if (pof->next_to_forward)
            pkt_to_send = pof->next_to_forward;
    }
    while (pof->queue_len > 0) {
        if ((pkt_to_send->seq == pof->pof_last_sent + 1) || (event & POF_TIMEOUT)) {
            if (pof->take_any)
               pof->take_any = false;
            pof_forward(pkt_to_send);
            if (event & POF_TIMEOUT) {
                event &= ~POF_TIMEOUT;
            }
            pof_pop_item(pkt_to_send);
        }
        else if (pkt_to_send->seq < pof->pof_last_sent + 1){
            pof_forward(pkt_to_send);
            pof_pop_item(pkt_to_send);
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
    if (next_deadline && timespeccmp(next_deadline, &now, >))
        timespecsub(next_deadline, &now, &timeout);
    else
        timeout = pof->pof_take_any_time;
    while (true) {
        int ret = ppoll(&fd, 1, &timeout, NULL);
        if (ret < 0) {
            perror("ppoll");
            continue;
        }
        /* pof_debug(pof); */
        pthread_mutex_lock(&pof->lock);
        if (fd.revents != 0 && (fd.revents & POLLIN)) {
            unsigned long event;
            ret = read(fd.fd, &event, sizeof(event));
            if (ret < 0) {
                perror("read");
                goto out;
            }
            if (event & POF_IN_ORDER_PKT) {
                pof_try_forward(pof, event);
            }
        } else if (ret == 0) { // POF timeout, packet deadline or take_any
            if (pof->queue_len != 0) {
                pof_try_forward(pof, POF_TIMEOUT);
                goto out;
            } else {// take_any
                pof_reset(pof);
            }
        }
out:
        next_deadline = get_next_deadline(pof);
        clock_gettime(CLOCK_REALTIME, &now);
        if (next_deadline && timespeccmp(next_deadline, &now, >))
            timespecsub(next_deadline, &now, &timeout);
        else
            timeout = pof->pof_take_any_time;
        pthread_mutex_unlock(&pof->lock);
    }

    return NULL;
}



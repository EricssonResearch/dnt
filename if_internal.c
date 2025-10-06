// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "if_internal.h"
#include "if_utils.h"
#include "interface.h"
#include "log.h"
#include "packet.h"
#include "parsetree.h"
#include "thread_utils.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/eventfd.h>
#include <pthread.h>

DEFAULT_LOGGING_MODULE(INTERFACE, INFO);
LOGGING_MODULE(PACKETTRACE, WARNING);

// fifo queue implemented as a circular linked list
struct PacketFifo {
    struct Packet *p;
    struct PacketFifo *prev;
    struct PacketFifo *next;
    pthread_mutex_t mutex; // only in the sentinel
};

static struct PacketFifo *new_packetfifo(void)
{
    // this initial node is a sentinel
    struct PacketFifo *ret = calloc_struct(PacketFifo);
    pthread_mutex_init(&ret->mutex, NULL);
    return ret;
}

static void packetfifo_insert(struct PacketFifo *pf, struct Packet *p)
{
    struct PacketFifo *n = calloc_struct(PacketFifo);
    n->p = p;
    pthread_mutex_lock(&pf->mutex);
    // new element is sentinel->next
    if (pf->next) {
        n->next = pf->next;
        n->prev = pf;
        pf->next->prev = n;
        pf->next = n;
    } else {
        n->prev = n->next = pf;
        pf->prev = pf->next = n;
    }
    pthread_mutex_unlock(&pf->mutex);
}

static struct Packet *packetfifo_get(struct PacketFifo *pf)
{
    // extracted element is sentinel->prev
    pthread_mutex_lock(&pf->mutex);
    if (pf->prev) {
        struct PacketFifo *del = pf->prev;
        struct Packet *p = del->p;
        if (pf->next == del) {
            // now we are empty
            pf->prev = pf->next = NULL;
        } else {
            // note: del->next = pf
            del->prev->next = del->next;
            del->next->prev = del->prev;
        }
        pthread_mutex_unlock(&pf->mutex);
        free(del);
        return p;
    } else {
        pthread_mutex_unlock(&pf->mutex);
        return NULL;
    }
}


static bool int_recv(struct Interface *iface)
{
    struct PacketFifo *pf = (struct PacketFifo *)iface->iface_private;
    uint64_t one;
    int ret = read(iface->recvfd, &one, 8);
    if (iface->state == IFS_SHUTDOWN) {
        return false;
    }
    if (ret < 0) {
        log_perror("read interface %s", iface->name);
        return false;
    }
    struct Packet *p = packetfifo_get(pf);

    // we receive on a single thread, no need for atomic
    iface->recv_packets += 1;
    iface->recv_octets += p->len;

    return iface_common_process(iface, p);
}

static void *int_recv_loop(void *arg)
{
    struct Interface *iface = (struct Interface *)arg;

    while (iface->state != IFS_SHUTDOWN)
        int_recv(iface);

    return NULL;
}

static bool int_send(struct Interface *iface, struct Packet *p)
{
    if (iface->state == IFS_INIT) {
        log_error("internal %s send: not opened yet", iface->name);
        return false;
    }

    if (p->header_count < 1) {
        log_error("internal %s send: packet doesn't have headers", iface->name);
        return false;
    }

    struct PacketFifo *pf = (struct PacketFifo *)iface->iface_private;
    PACKET_LOGCAT(p, "%s ", iface->name);

    __atomic_add_fetch(&iface->send_packets, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&iface->send_octets, packet_length(p), __ATOMIC_RELAXED);

    struct Packet *newp = serialize_packet(p);
    packetfifo_insert(pf, newp);
    uint64_t one = 1;
    int ret = write(iface->recvfd, &one, 8);
    if (ret < 0)
        log_perror("write interface %s", iface->name);
    return true;
}

static bool int_open(struct Interface *iface)
{
    if (iface->state != IFS_INIT) {
        log_error("open internal interface %s: already opened", iface->name);
        return false;
    }
    notification_register_source(iface->name, iface_notification_pull_fn, iface, 2000);
    iface->recvfd = eventfd(0, EFD_SEMAPHORE);
    iface->state = IFS_OPEN;
    iface->recv_th_ = thread_launch(int_recv_loop, iface, "rcv %s", iface->name);
    log_info("Internal interface %s", iface->name);
    return true;
}

static bool int_close(struct Interface *iface)
{
    struct PacketFifo *pf = (struct PacketFifo *)iface->iface_private;
    struct Packet *p;
    pthread_mutex_destroy(&pf->mutex); // only the sentinel has this
    while ((p = packetfifo_get(pf)) != NULL) {
        delete_packet(p);
    }
    free(pf); // the sentinel
    close(iface->recvfd);
    notification_register_source(iface->name, NULL, NULL, 2000);
    log_info("Internal interface %s closed", iface->name);
    return true;
}

struct Interface *new_internal_interface(const char *name)
{
    _NEW_IFACE(IF_INTERNAL);
    iface->ifname = NULL;
    iface->send = int_send;
    iface->open = int_open;
    iface->close_ = int_close;
    iface->get_property_reader = NULL;

    iface->iface_private = new_packetfifo();

    return iface;
}

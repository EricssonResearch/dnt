// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "if_internal.h"
#include "if_utils.h"
#include "interface.h"
#include "log.h"
#include "packet.h"
#include "parsetree.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/eventfd.h>

DEFAULT_LOGGING_MODULE(INTERFACE, INFO);

// fifo queue
struct PacketFifo {
    struct Packet *p;
    struct PacketFifo *prev;
    struct PacketFifo *next;
};

static struct PacketFifo *new_packetfifo(void)
{
    // this initial node is a sentinel
    return calloc_struct(PacketFifo);
}

static void packetfifo_insert(struct PacketFifo *pf, struct Packet *p)
{
    struct PacketFifo *n = calloc_struct(PacketFifo);
    n->p = p;
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
}

static struct Packet *packetfifo_get(struct PacketFifo *pf)
{
    // extracted element is sentinel->prev
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
        free(del);
        return p;
    } else {
        return NULL;
    }
}


static bool int_recv(struct Interface *iface)
{
    struct PacketFifo *pf = iface->iface_private;
    uint64_t one;
    int ret = read(iface->recvfd, &one, 8);
    if (ret < 0)
        log_perror("read interface %s", iface->name);
    struct Packet *p = packetfifo_get(pf);
    return iface_common_process(iface, p);
}

static bool int_send(struct Interface *iface, struct Packet *p)
{
    struct PacketFifo *pf = iface->iface_private;
    if (iface->state == IFS_OPEN) {
        struct Packet *newp = serialize_packet(p);
        packetfifo_insert(pf, newp);
        uint64_t one = 1;
        int ret = write(iface->recvfd, &one, 8);
        if (ret < 0)
            log_perror("write interface %s", iface->name);
    }
    return true;
}

static bool int_open(struct Interface *iface)
{
    if (iface->state != IFS_INIT) {
        log_error("open internal interface %s: already opened", iface->name);
        return false;
    }
    log_info("Internal interface %s", iface->name);
    iface->recvfd = eventfd(0, EFD_SEMAPHORE);
    iface->state = IFS_OPEN;
    return true;
}

static bool int_close(struct Interface *iface)
{
    struct PacketFifo *pf = iface->iface_private;
    struct Packet *p;
    while ((p = packetfifo_get(pf)) != NULL) {
        delete_packet(p);
    }
    free(pf); // the sentinel
    close(iface->recvfd);
    return true;
}

struct Interface *new_internal_interface(const char *name)
{
    struct Interface *iface = calloc_struct(Interface);
    iface->name = strdup(name);
    iface->ifname = NULL;
    iface->type = IF_INTERNAL;
    iface->state = IFS_INIT;
    iface->recv = int_recv;
    iface->send = int_send;
    iface->open = int_open;
    iface->close_ = int_close;
    iface->get_property_reader = NULL;

    iface->iface_private = new_packetfifo();

    iface->parsetree_ = new_parsetree(iface);
    parsetree_ref(iface->parsetree_);

    return iface;
}

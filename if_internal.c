
#include "if_internal.h"
#include "interface.h"
#include "packet.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/eventfd.h>

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


static struct Packet *int_recv(struct Interface *iface)
{
    struct PacketFifo *pf = iface->iface_private;
    uint64_t one;
    read(iface->recvfd, &one, 8);
    struct Packet *p = packetfifo_get(pf);
    return p;
}

static bool int_send(struct Interface *iface, struct Packet *p)
{
    struct PacketFifo *pf = iface->iface_private;
    if (iface->state == IFS_OPEN) {
        struct Packet *newp = serialize_packet(p);
        packetfifo_insert(pf, newp);
        uint64_t one = 1;
        write(iface->recvfd, &one, 8);
    }
    return true;
}

static bool int_open(struct Interface *iface)
{
    if (iface->state != IFS_INIT) {
        fprintf(stderr, "open internal interface %s: already opened\n", iface->name);
        return false;
    }
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

bool init_internal_interface(struct Interface *iface, const char *name)
{
    printf("init_internal_interface %s\n", name);
    bzero(iface, sizeof(*iface));
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

    return true;
}

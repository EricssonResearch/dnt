// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "interface.h"
#include "log.h"
#include "parsetree.h"
#include "pipeline.h"

#include <stdlib.h>

DEFAULT_LOGGING_MODULE(INTERFACE, INFO);

static void delete_interface(struct Interface *iface)
{
    iface->close_(iface);
    delete_parsetree(iface->parsetree_);
    free(iface->name);
    free(iface->ifname);
    free(iface);
}

void iface_ref(struct Interface *iface)
{
    __atomic_add_fetch(&iface->reference_count, 1, __ATOMIC_RELAXED);
}

void iface_unref(struct Interface *iface)
{
    int refcount = __atomic_sub_fetch(&iface->reference_count, 1, __ATOMIC_RELAXED);

    if (refcount == 0) {
        iface->parsetree_ = delete_parsetree(iface->parsetree_);
        log_debug("iface_unref %s senders %d\n", iface->name, iface->sender_count);
        if (iface->sender_count > 0) {
            iface->state = IFS_SHUTDOWN;
        } else {
            delete_interface(iface);
        }
    }
    //TODO if (refcount < 0) WTF
}

void iface_add_sender(struct Interface *iface)
{
    __atomic_add_fetch(&iface->sender_count, 1, __ATOMIC_RELAXED);
}

void iface_del_sender(struct Interface *iface)
{
    int sendercount = __atomic_sub_fetch(&iface->sender_count, 1, __ATOMIC_RELAXED);

    if (sendercount == 0) {
        if (iface->reference_count > 0) {
            // nothing
        } else {
            delete_interface(iface);
        }
    }
    //TODO if (sendercount < 0) WTF
}

bool iface_add_stream(struct Interface *iface, struct HeaderDescriptor *headers, struct Pipeline *pipe)
{
    if (iface->parsetree_ == NULL) {
        log_error("%s can't receive stream %s without a parsetree", iface->name, pipe->name);
        return false;
    }
    if (!parsetree_add_stream(iface->parsetree_, headers, pipe)) {
        log_error("failed to add stream %s to parsetree", pipe->name);
        return false;
    }
    pipeline_ref_send_interfaces(pipe);
    return true;
}

const char *iface_type_str(enum IfaceType type)
{
    switch (type) {
        case IF_ETH:
            return "Ethernet";
        case IF_INTERNAL:
            return "Internal";
        case IF_IP:
            return "IP";
        case IF_OAM:
            return "OAM Return";
        case IF_OAM_CMD:
            return "OAM Command";
        case IF_UDP_IN:
            return "UDP In";
        case IF_UDP_OUT:
            return "UDP Out";
    }
    return NULL;
}

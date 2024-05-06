// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "interface.h"
#include "log.h"
#include "parsetree.h"

#include <stdlib.h>

DEFAULT_LOGGING_MODULE(INTERFACE, INFO);

static void try_delete_interface(struct Interface *iface)
{
    if (iface->state == IFS_SHUTDOWN && iface->reference_count == 0) {
        iface->close_(iface);
        free(iface->name);
        free(iface->ifname);
        free(iface);
        //TODO somehow we need to signal the main thread that we are done
        //TODO the main thread should only exit when all interfaces gave the signal
    }
}


void close_iface(struct Interface *iface)
{
    if (iface->state == IFS_SHUTDOWN) {
        log_error("interface %s close called twice", iface->name);
        return;
    }
    iface->state = IFS_SHUTDOWN;
    // when the parsetree is done processing stuff
    //      it will unref its action pipelines
    // when the action pipelines have no more iterators
    //      they will unref their outgoing interfaces
    // finally we are free to close

    delete_parsetree(iface->parsetree_);
    try_delete_interface(iface);
}

void iface_ref(struct Interface *iface)
{
    __atomic_add_fetch(&iface->reference_count, 1, __ATOMIC_RELAXED);
}

void iface_unref(struct Interface *iface)
{
    __atomic_sub_fetch(&iface->reference_count, 1, __ATOMIC_RELAXED);
    try_delete_interface(iface);
}

bool iface_add_stream(struct Interface *iface, struct HeaderDescriptor *headers, struct Pipeline *pipe)
{
    if (iface->parsetree_ == NULL) {
        log_error("%s can't receive stream without a parsetree", iface->name);
        return false;
    }
    if (!parsetree_add_stream(iface->parsetree_, headers, pipe)) {
        log_error("failed to add stream to parsetree");
        return false;
    }
    return true;
}

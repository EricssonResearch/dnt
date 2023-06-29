// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "interface.h"
#include "parsetree.h"

#include <stdlib.h>
#include <stdio.h>

static void try_delete_interface(struct Interface *iface)
{
    if (iface->state == IFS_SHUTDOWN && iface->reference_count == 0) {
        iface->close_(iface);
        free(iface->name);
        free(iface->ifname);
        iface->state = IFS_DONE;
        //TODO somehow we need to signal the main thread that we are done
        //TODO the main thread should only exit when all interfaces gave the signal
    }
}

void iface_set_parsetree(struct Interface *iface, struct ParseTree *pt)
{
    if (iface->parsetree) {
        fprintf(stderr, "interface %s already has a parsetree\n", iface->name);
    } else {
        iface->parsetree = pt;
        parsetree_ref(pt);
    }
}


void close_iface(struct Interface *iface)
{
    if (iface->state == IFS_SHUTDOWN) {
        fprintf(stderr, "interface %s close called twice\n", iface->name);
        return;
    }
    if (iface->state == IFS_DONE) {
        fprintf(stderr, "interface close called when it was already deleted\n");
        return;
    }
    iface->state = IFS_SHUTDOWN;
    // when the parsetree is done processing stuff
    //      it will unref its action pipelines
    // when the action pipelines have no more iterators
    //      they will unref their outgoing interfaces
    // finally we are free to close
    if (iface->parsetree)
        parsetree_unref(iface->parsetree);
    try_delete_interface(iface);
}

void iface_ref(struct Interface *iface)
{
    __atomic_fetch_add(&iface->reference_count, 1, __ATOMIC_RELAXED);
}

void iface_unref(struct Interface *iface)
{
    if (iface->reference_count > 0)
        __atomic_fetch_sub(&iface->reference_count, 1, __ATOMIC_RELAXED);
    try_delete_interface(iface);
}

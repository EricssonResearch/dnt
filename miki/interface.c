
#include "interface.h"
#include "parsetree.h"

#include <stdlib.h>
#include <stdio.h>

static void try_delete_interface(struct Interface *iface)
{
    if (iface->shutdown == true && iface->reference_count == 0) {
        iface->close_(iface);
        free(iface->name);
        free(iface->ifname);
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
    iface->shutdown = true;
    //TODO unref the parsetree
    //      when the parsetree is done processing stuff
    //          it will unref its action pipelines
    //      when the action pipelines have no more iterators
    //          they will unref us
    //      finally we are free to close
    parsetree_unref(iface->parsetree);
    try_delete_interface(iface);
}

void iface_ref(struct Interface *iface)
{
    iface->reference_count++;
}

void iface_unref(struct Interface *iface)
{
    if (iface->reference_count > 0)
        iface->reference_count--;
    try_delete_interface(iface);
}

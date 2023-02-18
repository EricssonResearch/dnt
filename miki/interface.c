
#include "interface.h"

#include <stdlib.h>

static void try_delete_interface(struct Interface *iface)
{
    if (iface->shutdown == true && iface->reference_count == 0) {
        if (iface->del_)
            iface->del_(iface);
        free(iface->name);
        free(iface->ifname);
        //TODO somehow we need to signal that we are done
        //TODO the main thread should only exit when all interfaces gave the signal
    }
}

void close_interface(struct Interface *iface)
{
    iface->shutdown = true;
    try_delete_interface(iface);
}

void iface_ref(struct Interface *iface)
{
    iface->reference_count++;
}

void iface_unref(struct Interface *iface)
{
    iface->reference_count--;
    try_delete_interface(iface);
}

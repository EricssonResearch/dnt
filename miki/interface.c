
#include "interface.h"

#include <stdlib.h>


void fini_interface(struct Interface *iface)
{
    if (iface->del)
        iface->del(iface->iface_private);
    free(iface->name);
    free(iface->ifname);
}


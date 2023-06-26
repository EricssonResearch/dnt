// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "replicate.h"
#include "utils.h"

#include <stdlib.h>

struct Replicate {
    unsigned packets_passed;
};

struct Replicate *new_replicate(void)
{
    struct Replicate *ret = calloc_struct(Replicate);
    return ret;
}

struct Replicate *delete_replicate(struct Replicate *rep)
{
    free(rep);
    return NULL;
}

void replicate_packet_passed(struct Replicate *rep)
{
    __atomic_fetch_add(&rep->packets_passed, 1, __ATOMIC_RELAXED);
}

unsigned replicate_get_packets_passed(struct Replicate *rep)
{
    return rep->packets_passed;
}

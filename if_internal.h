// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_IF_INTERNAL_H
#define R2_IF_INTERNAL_H

#include <stdbool.h>

struct Interface;

bool init_internal_interface(struct Interface *iface, const char *name);

#endif // R2_IF_INTERNAL_H

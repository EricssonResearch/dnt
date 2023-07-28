// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_REPLICATE_H
#define R2_REPLICATE_H

#include "json.h"

// state object for replicate action
struct Replicate;

struct Replicate *new_replicate(void);

// always returns NULL
struct Replicate *delete_replicate(struct Replicate *rep);

void replicate_packet_passed(struct Replicate *rep);

struct JsonValue *replicate_get_state_json(const void *obj);

#endif // R2_REPLICATE_H

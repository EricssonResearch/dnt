// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_REPLICATE_H
#define R2_REPLICATE_H

// state object for replicate action
struct Replicate;

struct Replicate *new_replicate(void);

// always returns NULL
struct Replicate *delete_replicate(struct Replicate *rep);

void replicate_packet_passed(struct Replicate *rep);

unsigned replicate_get_packets_passed(struct Replicate *rep);

#endif // R2_REPLICATE_H

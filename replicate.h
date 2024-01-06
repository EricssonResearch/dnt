// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_REPLICATE_H
#define R2_REPLICATE_H

#include "object.h"
#include "packet.h"

struct PipelineObject *new_replicate(const char *name);

// always returns NULL
struct PipelineObject *delete_replicate(struct PipelineObject *rep);

//TODO receive PipelineIterator instead of packet to have uniform interface with POF
void replicate_packet_passed(struct PipelineObject *rep, struct Packet *p);


#endif // R2_REPLICATE_H

// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_DELAY_H
#define R2_DELAY_H

//#include <stdbool.h>

#include "pipeline.h"
#include "time_utils.h"
#include "notification.h"

extern int delay_actions;

// initialize the delay thread
bool init_delay(void);

// finish with the delay thread
void fini_delay(void);

// inserts the iterator @pi into the queue to be sent later
// @timestamp is the TSN timestamp for the moment when the packet entered our network
// @delay is the minimum time the packet must spend in the network
// the queue will release the packet when @delay has elapsed since @timestamp
// TODO return whether it has stored the packet or not (TODO return if no delaying needed)
void delay_insert(struct PipelineIterator *pi, unsigned timestamp, const struct timespec delay);

// registers a delay notification
bool register_delay_notification(bool add, char *target, unsigned period_ms);

#endif // R2_DELAY_H

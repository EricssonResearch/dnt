
#ifndef R2_DELAY_H

#include <stdbool.h>
#include <stddef.h>

#include "pipeline.h"


struct DelayBuffer;
//struct PipelineIterator;

// initialize the delay thread
bool init_delay(void);

// finish with the delay thread
void fini_delay(void);

void delay_insert(struct PipelineIterator *pi, unsigned timestamp, unsigned delay);


#define R2_DELAY_H
#endif // R2_DELAY_H

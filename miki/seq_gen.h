
#ifndef R2_SEQ_GEN_H
#define R2_SEQ_GEN_H

#include "header.h"

#include <stdbool.h>

struct SequenceGenerator;

struct SequenceGenerator *new_seq_gen(bool use_reset_flag, bool use_init_flag, unsigned init_seq);

// always returns NULL
struct SequenceGenerator *delete_seq_gen(struct SequenceGenerator *gen);

// this is a Producer function
// @state is struct SequenceGenerator
void seq_generator(void *state, value_consumer *consumer, void *consumer_state, struct Packet *p);

//TODO provide an example struct HeaderValue to show the config compiler what we will generate

#endif // R2_SEQ_GEN_H

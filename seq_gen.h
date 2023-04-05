
#ifndef R2_SEQ_GEN_H
#define R2_SEQ_GEN_H

#include "header.h"

#include <stdbool.h>

#define FRER_SEQ_GEN_RESET_FLAG_COUNT 3

struct SequenceGenerator;

struct SequenceGenerator *new_seq_gen(bool use_reset_flag, bool use_init_flag, unsigned init_seq);

// always returns NULL
struct SequenceGenerator *delete_seq_gen(struct SequenceGenerator *gen);

// this is a Producer function
// @state is struct SequenceGenerator
void seq_generator(struct SequenceGenerator *gen, struct Packet *p);

void sequence_generation_reset(struct SequenceGenerator *gen);

#endif // R2_SEQ_GEN_H

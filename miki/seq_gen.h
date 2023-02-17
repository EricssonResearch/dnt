
#ifndef R2_SEQ_GEN_H
#define R2_SEQ_GEN_H

#include "header.h"

#include <stdbool.h>

struct SequenceGenerator;

struct SequenceGenerator *new_seq_gen(bool use_reset_flag, bool use_init_flag, unsigned init_seq);

void seq_generator(struct Packet *p, struct HeaderField *target, field_assign *assign, void *state);



#endif // R2_SEQ_GEN_H

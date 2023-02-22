
#ifndef R2_HEADER_H
#define R2_HEADER_H

#include "transfer.h"

struct HeaderField {
    unsigned header_idx;
    unsigned bitoffset;
    unsigned bitcount;
};

// the Edit action has an array of these
// if @generator is NULL then it is a constant value
struct HeaderFieldAssign {
    value_consumer *assign;
    struct HeaderField target; // state of assign
    value_producer *generator;
    void *generator_state;
    struct Value constant;
    const char *text;
};

// @returns a suitable function for writing to @target field from @source
// the decision is based on the offsets and the lengths
// the HeaderField::header_idx is ignored
// the state of the assign function is a struct HeaderField
//TODO the config compiler will use this
value_consumer *get_assign_function(const struct HeaderField *target, const struct Value *source);

// returns a suitable function for reading this @source field into @target
// the HeaderField::header_idx is ignored
// the state of the reader function is a struct HeaderField
//TODO the config compiler will use this
//TODO how can the header matching use the returned function?
value_producer *get_read_function(const struct Value *target, const struct HeaderField *source);

#endif // R2_HEADER_H

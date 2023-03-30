
#ifndef R2_HEADER_H
#define R2_HEADER_H

#include "transfer.h"

struct ProtocolField;

struct HeaderField {
    unsigned header_idx;
    unsigned bitoffset;
    unsigned bitcount;
};

struct HeaderField *new_headerfield(unsigned header_idx, const struct ProtocolField *pfield);

// @returns a suitable function for writing to @target field from @source
// the decision is based on the offsets and the lengths
// the HeaderField::header_idx is ignored
// the state of the assign function should be @target
value_consumer *header_get_field_writer(const struct HeaderField *target, const struct Value *source);

// returns a suitable function for reading this @source field into @target
// the decision is based on the offsets and the lengths
// the HeaderField::header_idx is ignored
// the state of the reader function should be @source
//TODO how can the header matching use the returned function?
value_producer *header_get_field_reader(const struct Value *target, const struct HeaderField *source);

//TODO header_get_field_compare() for the parsetree matching

#endif // R2_HEADER_H

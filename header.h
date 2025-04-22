// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_HEADER_H
#define R2_HEADER_H

#include "protocol.h"
#include "value.h"

// identifies a field in a packet header
// @header_idx identifies which header of the packet to look at (starts with 0)
// @bitoffset is the number of bits to skip at the beginning of the header
// @bitcount is the number of bits the header field has
struct HeaderField {
    unsigned header_idx;
    unsigned bitoffset;
    unsigned bitcount;
};

// the caller has to make sure that @header_idx points to a header that has the correct type
struct HeaderField *new_headerfield(unsigned header_idx, const struct ProtocolField *pfield);

// @returns a suitable function for writing to @target field from @source
// the decision is based on the offsets and the lengths
// the HeaderField::header_idx is ignored
// the state of the returned writer function should be @target
value_consumer *header_get_field_writer(const struct HeaderField *target, const struct Value *source);

// returns a suitable function for reading this @source field into @target
// the decision is based on the offsets and the lengths
// the HeaderField::header_idx is ignored
// the state of the returned reader function should be @source
value_producer *header_get_field_reader(const struct Value *target, const struct HeaderField *source);

// @returns a suitable function for comparing @target field of the header with @match value
// the decision is based on the offsets and the lengths
// the HeaderField::header_idx is ignored
// the state of the returned comparator function should be @target
value_comparator *header_get_field_comprator(const struct HeaderField *target, const struct Value *match);

#endif // R2_HEADER_H

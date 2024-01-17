// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "header.h"
#include "log.h"
#include "packet.h"
#include "protocol.h"
#include "transfer.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

DEFAULT_LOGGING_MODULE(HEADERCONFIG, WARNING)

struct HeaderField *new_headerfield(unsigned header_idx, const struct ProtocolField *pfield)
{
    struct HeaderField *ret = calloc_struct(HeaderField);
    ret->header_idx = header_idx;
    ret->bitoffset = pfield->bitoffset;
    ret->bitcount = pfield->bitcount;
    return ret;
}

// only full bytes, no loose bits at the beginning or the end
static void write_bytes(void *state, struct Value *value, struct Packet *p)
{
    struct HeaderField *field = state;
    uint8_t *src = value->value + value->bitoffset/8;
    uint8_t *dst = p->buf + p->headers[field->header_idx].start + field->bitoffset/8;
    unsigned len = value->bitcount / 8;
    memcpy(dst, src, len);
}

// return true if data in the header and match equal
static bool compare_bytes(const void *state, const struct Value *value, const struct Packet *p)
{
    const struct HeaderField *field = state;
    uint8_t *match_data = value->value + value->bitoffset/8;
    uint8_t *hdr_data = p->buf + p->headers[field->header_idx].start + field->bitoffset/8;
    unsigned len = value->bitcount / 8;
    return !memcmp(hdr_data, match_data, len);
}

// set bits in a single byte
static void write_bits(void *state, struct Value *value, struct Packet *p)
{
    struct HeaderField *field = state;
    uint8_t *src = value->value + value->bitoffset/8;
    uint8_t *dst = p->buf + p->headers[field->header_idx].start + field->bitoffset/8;

    unsigned bitoffset = field->bitoffset % 8;
    unsigned bitcount = field->bitcount;
    unsigned shift = 8 - bitoffset - bitcount;
    unsigned char mask = 1 << bitcount; // we know bitcount < 8
    mask -= 1;
    mask <<= shift;
    dst[0] &= ~mask;
    dst[0] |= src[0] & mask;
}

// return true if bits in a single byte at the header equal with a given value
static bool compare_bits(const void *state, const struct Value *value, const struct Packet *p)
{
    const struct HeaderField *field = state;
    uint8_t *match_data = value->value + value->bitoffset/8;
    uint8_t *hdr_data = p->buf + p->headers[field->header_idx].start + field->bitoffset/8;

    unsigned bitoffset = field->bitoffset % 8;
    unsigned bitcount = field->bitcount;
    unsigned shift = 8 - bitoffset - bitcount;
    unsigned char mask = 1 << bitcount; // we know bitcount < 8
    mask -= 1;
    mask <<= shift;
    return (hdr_data[0] & mask) == (match_data[0] & mask);

}

// generic writer of any number of bits over any number of bytes
static void write_generic(void *state, struct Value *value, struct Packet *p)
{
    struct HeaderField *field = state;
    uint8_t *src = value->value + value->bitoffset/8;
    uint8_t *dst = p->buf + p->headers[field->header_idx].start + field->bitoffset/8;

    unsigned bitoffset = field->bitoffset % 8;
    unsigned bitcount = field->bitcount; // total bits to write
    unsigned bitcount1 = MIN(bitcount, 8 - bitoffset); // bits in first byte
    unsigned shift = 8 - bitoffset - bitcount1;
    unsigned char mask = 1 << bitcount1;
    mask -= 1;
    mask <<= shift;
    dst[0] &= ~mask;
    dst[0] |= src[0] & mask;
    unsigned remaining_bits = bitcount - bitcount1;
    unsigned remaining_bytes = remaining_bits / 8;
    unsigned byteoffset = 1;

    if (remaining_bytes) {
        memcpy(dst+1, src+1, remaining_bytes);
        remaining_bits -= remaining_bytes * 8;
        byteoffset += remaining_bytes;
    }

    if (remaining_bits) {
        shift = 8 - remaining_bits;
        mask = 1 << remaining_bits;
        mask -= 1;
        mask <<= shift;
        dst[byteoffset] &= ~mask;
        dst[byteoffset] |= src[byteoffset] & mask;
    }
}

static bool compare_generic(const void *state, const struct Value *value, const struct Packet *p)
{
    bool match = true;
    const struct HeaderField *field = state;
    uint8_t *match_data = value->value + value->bitoffset/8;
    uint8_t *hdr_data = p->buf + p->headers[field->header_idx].start + field->bitoffset/8;

    unsigned bitoffset = field->bitoffset % 8;
    unsigned bitcount = field->bitcount; // total bits to compare
    unsigned bitcount1 = MIN(bitcount, 8 - bitoffset); // bits in first byte
    unsigned shift = 8 - bitoffset - bitcount1;
    unsigned char mask = 1 << bitcount1;
    mask -= 1;
    mask <<= shift;
    match &= ((hdr_data[0] & mask) == (match_data[0] & mask));
    unsigned remaining_bits = bitcount - bitcount1;
    unsigned remaining_bytes = remaining_bits / 8;
    unsigned byteoffset = 1;

    if (remaining_bytes) {
        match &= !memcmp(hdr_data+1, match_data+1, remaining_bytes);
        remaining_bits -= remaining_bytes * 8;
        byteoffset += remaining_bytes;
    }

    if (remaining_bits) {
        shift = 8 - remaining_bits;
        mask = 1 << remaining_bits;
        mask -= 1;
        mask <<= shift;
        match &= ((hdr_data[byteoffset] & mask) == (match_data[byteoffset] & mask));
    }

    return match;
}

value_consumer *header_get_field_writer(const struct HeaderField *target, const struct Value *source)
{
    if (source->bitcount != target->bitcount) {
        log_error("field writer: source and target has different bit count %u %u\n",
                source->bitcount, target->bitcount);
        return NULL;
    }
    if ((source->bitoffset % 8) != (target->bitoffset % 8)) {
        log_error("field writer: source and target has different bit offset %u %u\n",
                (source->bitoffset % 8), (target->bitoffset % 8));
        return NULL;
    }

    // octet-based assignment
    if ((target->bitoffset % 8) == 0 && (target->bitcount % 8) == 0 &&
            (source->bitoffset % 8) == 0 && (source->bitcount % 8) == 0)
        return write_bytes;

    // some bits within a single byte
    if (target->bitoffset % 8 + target->bitcount <= 8)
        return write_bits;

    // anything else
    return write_generic;
}

value_comparator *header_get_field_comprator(const struct ProtocolField *target, const struct Value *match)
{
    if (match->bitcount != target->bitcount) {
        log_error("field writer: source and target has different bit count %u %u\n",
                match->bitcount, target->bitcount);
        return NULL;
    }
    if ((match->bitoffset % 8) != (target->bitoffset % 8)) {
        log_error("field writer: source and target has different bit offset %u %u\n",
                (match->bitoffset % 8), (target->bitoffset % 8));
        return NULL;
    }

    // octet-based comparison
    if ((target->bitoffset % 8) == 0 && (target->bitcount % 8) == 0 &&
            (match->bitoffset % 8) == 0 && (match->bitcount % 8) == 0)
        return compare_bytes;

    // compare some bits within a single byte
    if (target->bitoffset % 8 + target->bitcount <= 8)
        return compare_bits;

    // anything else
    return compare_generic;
}

// points to the first byte of the field
static void read_bytes(void *state, value_consumer *consumer, void *consumer_state, struct Packet *p)
{
    struct HeaderField *source = state;
    uint8_t *src = p->buf + p->headers[source->header_idx].start + source->bitoffset/8;
    struct Value val = {src, source->bitoffset%8, source->bitcount};
    consumer(consumer_state, &val, p);
}

value_producer *header_get_field_reader(const struct Value *target, const struct HeaderField *source)
{
    if (source->bitcount != target->bitcount) {
        log_error("field reader: source and target has different bit count %u %u\n",
                source->bitcount, target->bitcount);
        return NULL;
    }
    if ((source->bitoffset % 8) != (target->bitoffset % 8)) {
        log_error("field reader: source and target has different bit offset %u %u\n",
                (source->bitoffset % 8), (target->bitoffset % 8));
        return NULL;
    }

    // this should be good for all cases
    return read_bytes;
}

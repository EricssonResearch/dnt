// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_VALUE_H
#define R2_VALUE_H


#include "packet.h"

#include <stddef.h>
#include <stdbool.h>

// describes the value passed from a Producer function to a Consumer function
// the Edit action does packet header manipulations with Value objects
// the Parsetree does packet header comparisons with Value objects
// @value is a pointer to a buffer (network byte order, LSB-first)
// @bitoffset is the number of bits to skip at the beginning of @value
// @bitcount is the number of bits the value has (not necessarily multiple of 8)
struct Value {
    void *value;
    unsigned bitoffset;
    unsigned bitcount;
};


// this struct is typically stored by value in other structs, so no allocation here
static inline struct Value init_value(unsigned bitoffset, unsigned bitcount)
{
    struct Value v;
    v.value = NULL;
    v.bitoffset = bitoffset;
    v.bitcount = bitcount;
    return v;
}

//TODO what is the state of the various producer/consumer types?
//  header field: Packet, HeaderField <- this is the only one that needs two states
//  packet metadata (TODO we removed this long ago): Packet
//  interface property (Producer only): Interface
//  value generator/consumer object (TODO we removed these long ago): the object (SeqGen, SeqRcvy)
//  constant: doesn't need a producer at all

// prototype for a Consumer function that receives a @value
typedef void value_consumer(void *state, struct Value *value, struct Packet *p);

// prototype for a Producer function that creates a value, and hands it to the @consumer
typedef void value_producer(void *state, value_consumer *consumer, void *consumer_state, struct Packet *p);

// prototype for a Comparator function that returns true if the received @value is the same as its own
// @state can be used to hold the "own" value
typedef bool value_comparator(const void *state, const struct Value *value, const struct Packet *p);

#endif // R2_VALUE_H

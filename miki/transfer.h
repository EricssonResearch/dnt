
#ifndef R2_TRANSFER_H
#define R2_TRANSFER_H

//TODO rename this to value.h ?

#include <stddef.h>

// describes the value passed from the Producer to the Consumer
// @value must be stored in network byte order
// in some cases @value is NULL when we only need to remember the offsets
struct Value {
    void *value;
    unsigned bitoffset;
    unsigned bitcount;
};

struct Packet;

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
//  packet metadata: Packet
//  interface property: Interface
//  value generator/consumer object: the object (SeqGen, SeqRcvy)
//  constant: doesn't need a producer at all

// prototype for a Consumer function
typedef void value_consumer(void *state, struct Value *value, struct Packet *p);

// prototype for a Producer function
typedef void value_producer(void *state, value_consumer *consumer, void *consumer_state, struct Packet *p);

//TODO value_compare?

#endif // R2_TRANSFER_H

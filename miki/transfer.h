
#ifndef R2_TRANSFER_H
#define R2_TRANSFER_H

// describes the value passed from the Producer to the Consumer
// the value must be stored in network byte order
struct Value {
    void *value;
    unsigned bitoffset;
    unsigned bitcount;
};

struct Packet;

// prototype for a Consumer function
typedef void value_consumer(void *state, struct Value *value, struct Packet *p);

// prototype for a Producer function
typedef void value_producer(void *state, value_consumer *consumer, void *consumer_state, struct Packet *p);

//TODO value_compare?

#endif // R2_TRANSFER_H

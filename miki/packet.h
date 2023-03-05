
#ifndef R2_PACKET_H
#define R2_PACKET_H

#include "transfer.h"

#include <stdbool.h>
#include <time.h>

//TODO move the defines in a central location?
#define PACKET_BUF_LEN 10000
#define PACKET_MAX_HEADER_NUM 20
#define PACKET_COUNT_LIMIT 200
#define PACKET_START_OFFSET 1024

struct Interface;

//TODO how can we mark something as payload?
//  use type=0? no, we need typed payload
struct PacketHeader {
    int type;
    unsigned start; // from beginning of buf
    unsigned len;
};

struct Packet {
    unsigned char *buf;
    unsigned start; // from beginning of buf
    unsigned len;

    // scratch space starts at offset 0
    unsigned scratch_len;

    struct PacketHeader headers[PACKET_MAX_HEADER_NUM];
    unsigned header_count;

    struct Interface *from;
    struct timespec recv_time;

    // packet properties that Edit can read-write
    unsigned timestamp; // holds a ttag
    unsigned sequence;  // holds a rtag
};

struct Packet *new_packet(struct Interface *from);

// always returns NULL
struct Packet *delete_packet(struct Packet *p);

// returns a deep copy of the packet
struct Packet *copy_packet(const struct Packet *p);

// returns true if the packet is a dummy buffer
bool packet_dummy(const struct Packet *p);

// identify the header at the given position in the packet as @type
// this will create a new entry in p->headers
// @offset is counted from p->start
// the headers MUST be identified by increasing offset
// TODO do we prevent the headers from overlapping? do we have to?
void packet_identify_header(struct Packet *p, int type, unsigned offset, unsigned len);

// adds a new header on the scratch space
// the position in the header list is @idx
// all the existing headers after @idx will be shifted in the array
void packet_add_header(struct Packet *p, unsigned idx, int type, unsigned len);

// removes a header and forgets it
// all the headers after @idx will be shifted in the array
void packet_del_header(struct Packet *p, unsigned idx);

// @returns FT_UNKNOWN for invalid property name
enum ProtocolFieldType packet_get_property_type(const char *name);

// @returns a consumer function to write the given packet property
// the size and offset of @source is checked for compatibility
// the consumer has no state other than the packet
// @returns NULL if @name is invalid or @source is incompatible
value_consumer *packet_get_property_writer(const char *name, struct Value *source);

// @returns a producer function to read the given packet property
// the size and offset of @target is checked for compatibility
// the producer has no state other than the packet
// @returns NULL if @name is invalid or @target is incompatible
value_producer *packet_get_property_reader(const char *name, struct Value *target);


#endif // R2_PACKET_H

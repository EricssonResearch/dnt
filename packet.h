// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


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

struct PacketHeader {
    int type;
    unsigned start; // from beginning of buf
    unsigned len;
};

struct Packet {
    // this holds the received packet and a scratch space
    unsigned char *buf;

    // these two are set by the Interface upon reception
    // use @headers to locate stuff after the headers have been identified
    // @start should be PACKET_START_OFFSET
    unsigned start; // from beginning of buf
    unsigned len;

    // newly added headers are created on the scratch space that
    // begins at offset 0 in @buf
    unsigned scratch_len; // allocated bytes on the scratch space

    // list of headers identified in the packet
    // the last one should be PROTO_ID_PAYLOAD
    struct PacketHeader headers[PACKET_MAX_HEADER_NUM];
    unsigned header_count;

    struct Interface *from;
    struct timespec recv_time; // Delay action uses this

    // packet properties that can be accessed via dedicated read/write actions
    unsigned timestamp; // holds a ttag
    unsigned sequence;  // holds a rtag
};

// returns a newly allocated packet that has a buffer
// do not process it if packet_dummy() returns false!
struct Packet *new_packet(struct Interface *from);

// always returns NULL
struct Packet *delete_packet(struct Packet *p);

// returns a deep copy of the packet
struct Packet *copy_packet(const struct Packet *p);

// retuns a new packet that is a serialization of the headers in @p
struct Packet *serialize_packet(struct Packet *p);

// returns true if the packet is a dummy buffer
bool packet_dummy(const struct Packet *p);

// identify the header at the given position in the packet as @type
// this will create a new entry in p->headers
// @offset is counted from p->start not from p->buf!
// the headers MUST be identified by increasing offset
// TODO do we prevent the headers from overlapping? do we have to?
void packet_identify_header(struct Packet *p, int type, unsigned offset, unsigned len);

// adds a new header on the scratch space, adds an entry to @p->headers
// the position in the header list is @idx
// all the existing headers after @idx will be shifted in the array
void packet_add_header(struct Packet *p, unsigned idx, int type, unsigned len);

// removes a header and forgets it in @p->headers
// all the headers after @idx will be shifted in the array
void packet_del_header(struct Packet *p, unsigned idx);

// remove all headers from @p->headers
void packet_clear_headers(struct Packet *p);

// prints a warning if there are too many packets in the system
void packets_check_performance(void);

#endif // R2_PACKET_H

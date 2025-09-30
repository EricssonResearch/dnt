// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_PACKET_H
#define R2_PACKET_H

#include "protocol.h"

#include <stdbool.h>
#include <time.h>

//TODO move the defines in a central location?
#define PACKET_BUF_LEN 10000
#define PACKET_LOG_BUF_SIZE 256
#define PACKET_MAX_HEADER_NUM 20
#define PACKET_COUNT_LIMIT 200
#define PACKET_START_OFFSET 1024

struct Interface;

struct PacketHeader {
    enum ProtocolID type;
    unsigned start; // from beginning of buf
    unsigned len;
};

struct Packet {
    // this holds the received packet and a scratch space
    // the scratch space starts at offset 0
    // the receive space starts at PACKET_START_OFFSET
    // the total size of the buffer is PACKET_BUF_LEN
    unsigned char *buf;

    // these two tell where the received packet is in @buf
    // they are set by the Interface upon reception
    // use @headers to locate stuff after the headers have been identified
    // @start is usually PACKET_START_OFFSET
    // @len should be treated as constant
    unsigned start; // from beginning of @buf
    unsigned len; // number of used bytes after @start

    // newly added headers are created on the scratch space
    // the scratch space begins at offset 0 in @buf
    unsigned scratch_len; // allocated bytes on the scratch space

    // list of headers in the packet
    // headers can be in the receive space or the scratch space
    // the last header is typically PROTO_ID_PAYLOAD
    struct PacketHeader headers[PACKET_MAX_HEADER_NUM];
    unsigned header_count;

    struct Interface *from;
    struct timespec recv_time; // time when the packet was received
    unsigned id; // uniquely identifies a packet
    unsigned original_id; // copy operations assign new id, this is the original one

    // packet properties that can be accessed via dedicated read/write actions
    unsigned timestamp; // holds a ttag/tcw (initially the receive time, use readtstamp)
    unsigned sequence;  // holds a rtag/dcw (initially zero, use readseq/seqgen)

    // filled by the ttlreduce action, verified by the ttlcheck action
    unsigned ttl;

    // log buffer for packet logging
    char logbuf[PACKET_LOG_BUF_SIZE];

    // filled by the delay action
    bool offload;
    struct timespec delay;
};

// @returns a newly allocated packet that has its own buffer
// the buffer is dummy, if there are too many packets already
struct Packet *new_packet(struct Interface *from);

// @returns a newly allocated packet that has its own buffer
// the buffer is dummy, if there are too many packets already
// this version doesn't init the buffer to improve receive latency
struct Packet *new_packet_fast(struct Interface *from);

// always @returns NULL
struct Packet *delete_packet(struct Packet *p);

// @returns a deep copy of the packet
// the new packet has the same buffer layout and headers as @p
// the new packet has new @id, its @original_id is the same as in @p
struct Packet *copy_packet(const struct Packet *p);

// returns the packet length calculated from the header list
unsigned packet_length(const struct Packet *p);

// @returns a new packet that is a serialization of the headers in @p
// the new packet has every header data in correct order in the receive space
// the result looks like it was received on the wire
// the new packet has new @id, its @original_id is the same as in @p
struct Packet *serialize_packet(const struct Packet *p);

// @returns true if the packet is a dummy buffer
// we allow a finite number (PACKET_COUNT_LIMIT) of packets to exist at the same time
// newly created packets over the limit are dummy buffers that should not be processed
bool packet_dummy(const struct Packet *p);

// identify the header at the given position in the received packet as @type
// this will create a new entry in p->headers
// @offset and @len are bytes
// @offset is counted from p->start not from p->buf!
// @offset + @len MUST NOT exceed p->len
// the headers MUST be identified by increasing offset
// @returns false on error
bool packet_identify_header(struct Packet *p, enum ProtocolID type, unsigned offset, unsigned len);

// adds a new header on the scratch space, adds an entry to @p->headers
// the position in the header list is @idx
// all the existing entries in p->headers after @idx will be shifted in the array
// @returns false on error
bool packet_add_header(struct Packet *p, unsigned idx, enum ProtocolID type, unsigned len);

// removes a header and forgets it in @p->headers
// the header can be in the receive space or the scratch space
// all the headers after @idx will be shifted in the array
void packet_del_header(struct Packet *p, unsigned idx);

// remove all headers from @p->headers
// also clears the scratch space
void packet_clear_headers(struct Packet *p);

// if p->len is 0, move p->start to the end of the buffer
// this is useful when the packet is locally generated:
//  there is no received data at p->start (p->len is 0)
//  the payload we build is on the scratch space
void packet_enlarge_scratch(struct Packet *p);

// @returns true if the headers in @p are continuous in memory
// the headers can be in the receive area or in the scratch space
bool packet_is_linear(const struct Packet *p);

// prints a warning if there are too many packets in the system
void packets_check_performance(void);

// append text messages to the packet's log buffer
// must declare the PACKETTRACE log module wherever this is used
#define PACKET_LOGCAT(...) \
    if (log_enabled_m(PACKETTRACE, PACKET)) \
            _packet_logcat(__VA_ARGS__)

// private, use PACKET_LOGCAT
void _packet_logcat(struct Packet *p, const char *frmt, ...)
    __attribute__((format(printf, 2, 3)))
    __attribute__((nonnull(2)));

// print the packet log buffer to the appropriate output
void packet_printlog(const struct Packet *p);

#endif // R2_PACKET_H

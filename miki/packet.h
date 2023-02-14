
#ifndef R2_PACKET_H
#define R2_PACKET_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <time.h>

//TODO move the defines in a central location?
#define PACKET_BUF_LEN 10000
#define PACKET_MAX_HEADER_NUM 20
#define PACKET_COUNT_LIMIT 200
#define PACKET_START_OFFSET 1024

struct Interface;

struct PacketHeader {
    int type;
    unsigned char *start;
    size_t len;
};

struct Packet {
    unsigned char *buf;
    off_t start; //TODO pointer?
    size_t len;

    // scratch space starts at offset 0
    size_t scratch_len;

    struct PacketHeader headers[PACKET_MAX_HEADER_NUM];
    unsigned header_count;

    struct Interface *from;
    struct timespec arrival_time;
};

struct Packet *new_packet(struct Interface *from);

struct Packet *delete_packet(struct Packet *p);

// returns a deep copy of the packet
struct Packet *copy_packet(const struct Packet *p);

// returns true if the packet is a dummy buffer
bool packet_dummy(const struct Packet *p);

// identify the header at the given position in the packet as @type
// this will create a new entry in p->headers
// @offset is counted from p->start
// the entries of p->headers will be ordered by their offset
// TODO do we prevent the headers from overlapping? do we have to?
void packet_identify_header(struct Packet *p, int type, off_t offset, size_t len);

// adds a new header on the scratch space
// the position in the header list is @idx
// all the existing headers after @idx will be shifted in the array
void packet_add_header(struct Packet *p, unsigned idx, int type, size_t len);

// removes a header and forgets it
// all the headers after @idx will be shifted in the array
void packet_del_header(struct Packet *p, unsigned idx);


#endif // R2_PACKET_H

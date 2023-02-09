
#include "packet.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>

static unsigned packet_count = 0;
static unsigned char *dummybuf = NULL;

struct Packet *new_packet(struct Interface *from)
{
    //TODO global constructor function?
    //TODO delete in a global destructor function?
    if (!dummybuf) dummybuf = calloc(1, PACKET_BUF_LEN);

    struct Packet *ret = calloc_struct(Packet);
    if (packet_count >= PACKET_COUNT_LIMIT) {
        ret->buf = dummybuf;
    } else {
        ret->buf = calloc(1, PACKET_BUF_LEN);
        packet_count++;
    }
    // note: malloc returns pointers aligned to be suitable for long double
    // this offset is divisible with 4, so the start position is okay
    ret->start = PACKET_START_OFFSET;
    ret->from = from;
    return ret;
}

struct Packet *delete_packet(struct Packet *p)
{
    if (!p) return NULL;
    if (p->buf != dummybuf) {
        free(p->buf);
        packet_count--;
    }
    free(p);
    return NULL;
}

struct Packet *copy_packet(const struct Packet *p)
{
    struct Packet *newp = calloc_struct(Packet);
    *newp = *p;
    // note: the newly allocated buf is aligned the same was as the old one
    newp->buf = memdup(p->buf, PACKET_BUF_LEN);

    // adjust the start pointers in the header array to point to the new buffer
    for (unsigned i=0; i<p->header_count; i++) {
        ptrdiff_t diff = p->headers[i].start - p->buf;
        newp->headers[i].start = newp->buf + diff;
    }

    return newp;
}

bool packet_dummy(const struct Packet *p)
{
    return p->buf == dummybuf;
}

//TODO implement this
void packet_identify_header(struct Packet *p, int type, off_t offset, size_t len);

// returns NULL if we are out of scratch space
//TODO the config compiler can check that scratch allocations are fine
static unsigned char *scratch_alloc(struct Packet *p, size_t len)
{
    unsigned char *ret = p->buf + p->scratch_len;
    if (p->scratch_len + len >= PACKET_START_OFFSET) return NULL;
    p->scratch_len += len;
    return ret;
}

//TODO static void scratch_free(struct Packet *p, unsigned char *start)

void packet_add_header(struct Packet *p, unsigned idx, int type, size_t len)
{
    unsigned char *start = scratch_alloc(p, len);
    //TODO if (start == NULL) error
    //TODO if (p->header_count == PACKET_MAX_HEADER_NUM) error
    //TODO if (idx > p->header_count) error

    if (idx < p->header_count)
        memmove(p->headers+idx+1, p->headers+idx,
                (p->header_count-idx)*sizeof(struct PacketHeader));
    p->headers[idx].type = type;
    p->headers[idx].start = start;
    p->headers[idx].len = len;
    p->header_count++;
}

void packet_del_header(struct Packet *p, unsigned idx)
{
    //TODO if (idx >= p->header_count) error

    if (idx < p->header_count)
        memmove(p->headers+idx, p->headers+idx+1,
                (p->header_count-idx-1)*sizeof(struct PacketHeader));
    p->header_count--;
}


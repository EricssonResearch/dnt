
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
#if 0
    newp->buf = memdup(p->buf, PACKET_BUF_LEN);
#else
    // optimization: only copy the packet and the scratch
    newp->buf = malloc(PACKET_BUF_LEN);
    // we need to keep the unallocated scratch space zeroed
    memcpy(newp->buf, p->buf, p->start+p->len);
#endif
    return newp;
}

bool packet_dummy(const struct Packet *p)
{
    return p->buf == dummybuf;
}

//TODO implement this
void packet_identify_header(struct Packet *p, int type, off_t offset, size_t len);

static off_t scratch_alloc(struct Packet *p, size_t len)
{
    if (p->scratch_len + len >= PACKET_START_OFFSET) return -1;
    off_t ret = p->scratch_len;
    p->scratch_len += len;
    return ret;
}

//TODO static void scratch_free(struct Packet *p, unsigned char *start)

void packet_add_header(struct Packet *p, unsigned idx, int type, size_t len)
{
    if (p->header_count == PACKET_MAX_HEADER_NUM) {
        //TODO error (can we prevent this in the config compiler?)
    }
    if (idx > p->header_count) {
        //TODO error (this should never happen though)
    }
    off_t start = scratch_alloc(p, len);
    if (start < 0) {
        //TODO error: out of scratch space TODO how to handle this??
    }

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
    if (idx >= p->header_count) {
        //TODO error (this should never happen though)
    }

    if (idx < p->header_count)
        memmove(p->headers+idx, p->headers+idx+1,
                (p->header_count-idx-1)*sizeof(struct PacketHeader));
    p->header_count--;
}


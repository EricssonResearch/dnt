// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "packet.h"
#include "protocol.h"
#include "utils.h"
#include "log.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

DEFAULT_LOGGING_MODULE(MAIN, WARNING)
LOGGING_MODULE(OAM, WARNING)

static unsigned packet_count = 0;
static unsigned char *dummybuf = NULL;
static unsigned next_packet_id = 0;

static void __attribute__((destructor)) free_dummybuf(void)
{
    free(dummybuf);
}

struct Packet *new_packet(struct Interface *from)
{
    if (!dummybuf) dummybuf = calloc(1, PACKET_BUF_LEN);

    struct Packet *ret = calloc_struct(Packet);
    if (packet_count >= PACKET_COUNT_LIMIT) {
        ret->buf = dummybuf;
    } else {
        ret->buf = calloc(1, PACKET_BUF_LEN);
        __atomic_fetch_add(&packet_count, 1, __ATOMIC_RELAXED);
    }
    // note: malloc returns pointers aligned to be suitable for long double
    // this offset is divisible with 4, so the start position is okay
    ret->start = PACKET_START_OFFSET;
    ret->from = from;
    ret->id = __atomic_fetch_add(&next_packet_id, 1, __ATOMIC_RELAXED);
    ret->original_id = ret->id;
    return ret;
}

struct Packet *delete_packet(struct Packet *p)
{
    if (!p) return NULL;
    if (p->buf != dummybuf) {
        free(p->buf);
        __atomic_fetch_sub(&packet_count, 1, __ATOMIC_RELAXED);
    }
    free(p);
    return NULL;
}

struct Packet *copy_packet(const struct Packet *p)
{
    struct Packet *newp = calloc_struct(Packet);
    *newp = *p;
    newp->id = __atomic_fetch_add(&next_packet_id, 1, __ATOMIC_RELAXED);
#if 0
    newp->buf = memdup(p->buf, PACKET_BUF_LEN);
#else
    // optimization: only copy the packet and the scratch
    newp->buf = malloc(PACKET_BUF_LEN);
    // we need to keep the unallocated scratch space zeroed
    memcpy(newp->buf, p->buf, p->start+p->len);
#endif
    __atomic_fetch_add(&packet_count, 1, __ATOMIC_RELAXED);
    return newp;
}

struct Packet *serialize_packet(const struct Packet *p)
{
    struct Packet *ret = calloc_struct(Packet);
    *ret = *p;

    ret->buf = calloc(1, PACKET_BUF_LEN);
    ret->start = PACKET_START_OFFSET;
    unsigned dstlen = 0;
    for (unsigned i=0; i<p->header_count; i++) {
        unsigned char *src = p->buf + p->headers[i].start;
        unsigned char *dst = ret->buf + PACKET_START_OFFSET + dstlen;
        unsigned len = p->headers[i].len;
        memcpy(dst, src, len);
        dstlen += len;
    }
    ret->len = dstlen;

    ret->id = __atomic_fetch_add(&next_packet_id, 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&packet_count, 1, __ATOMIC_RELAXED);
    return ret;
}

bool packet_dummy(const struct Packet *p)
{
    return p->buf == dummybuf;
}

void packet_identify_header(struct Packet *p, enum ProtocolID type, unsigned offset, unsigned len)
{
    if (p->header_count == PACKET_MAX_HEADER_NUM) {
        log_debug("packet_identify_header: already at maximum header count\n");
        return;
    }
    if (p->header_count > 0 && p->headers[p->header_count-1].start > p->start + offset) {
        log_debug("packet_identify_header: new offset % is smaller than the previous %u\n",
                offset, p->headers[p->header_count-1].start);
        return;
    }
    struct PacketHeader *h = p->headers+p->header_count;
    h->type = type;
    //TODO check that h->start + h->len < p->start + p->len
    h->start = p->start + offset;
    h->len = len;
    p->header_count++;
}

static int scratch_alloc(struct Packet *p, unsigned len)
{
    if (p->scratch_len + len >= PACKET_START_OFFSET) return -1;
    off_t ret = p->scratch_len;
    p->scratch_len += len;
    return ret;
}

//TODO static void scratch_free(struct Packet *p, unsigned char *start)

void packet_add_header(struct Packet *p, unsigned idx, enum ProtocolID type, unsigned len)
{
    if (p->header_count == PACKET_MAX_HEADER_NUM) {
        //TODO error (can we prevent this in the config compiler?)
    }
    if (idx > p->header_count) {
        //TODO if this error happens, the config compiler is broken
        log_debug("packet_add_header index too large %u > %u\n", idx, p->header_count);
        return;
    }
    int start = scratch_alloc(p, len);
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

void packet_clear_headers(struct Packet *p)
{
    memset(p->headers, 0, p->header_count * sizeof(struct PacketHeader));
    p->header_count = 0;
}

void packets_check_performance(void)
{
    if (packet_count > PACKET_COUNT_LIMIT * 0.9) {
        log_warning_m(OAM, "\033[0;31mSEVERE PERFORMANCE WARNING: too many packets in the system\033[0m\n");
    } else if (packet_count > PACKET_COUNT_LIMIT * 0.5) {
        log_warning_m(OAM, "\033[0;33mPERFORMANCE WARNING: too many packets in the system\033[0m\n");
    }
    //else log_warning_m(OAM, "\033[0;32mPERFORMANCE okay\033[0m\n");
}

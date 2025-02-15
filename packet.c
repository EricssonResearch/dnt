// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "packet.h"
#include "notification.h"
#include "protocol.h"
#include "utils.h"
#include "log.h"

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

DEFAULT_LOGGING_MODULE(PACKET, WARNING);
LOGGING_MODULE(PACKETTRACE, WARNING);

static unsigned packet_count = 0;
static unsigned char *dummybuf = NULL;
static unsigned next_packet_id = 0;

static void __attribute__((destructor)) free_dummybuf(void)
{
    free(dummybuf);
}

struct Packet *new_packet(struct Interface *from)
{
    if (!dummybuf) dummybuf = (unsigned char *)calloc(1, PACKET_BUF_LEN);

    struct Packet *ret = calloc_struct(Packet);
    if (packet_count >= PACKET_COUNT_LIMIT) {
        ret->buf = dummybuf;
    } else {
        ret->buf = (unsigned char *)calloc(1, PACKET_BUF_LEN);
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
    packet_printlog(p);
    free(p);
    return NULL;
}

struct Packet *copy_packet(const struct Packet *p)
{
    struct Packet *newp = calloc_struct(Packet);
    *newp = *p;
    newp->id = __atomic_fetch_add(&next_packet_id, 1, __ATOMIC_RELAXED);
#if 0
    newp->buf = (unsigned char *)memdup(p->buf, PACKET_BUF_LEN);
#else
    // optimization: only copy the packet and the scratch
    newp->buf = (unsigned char *)malloc(PACKET_BUF_LEN);
    // we need to keep the unallocated scratch space zeroed
    memcpy(newp->buf, p->buf, p->start+p->len);
#endif
    __atomic_fetch_add(&packet_count, 1, __ATOMIC_RELAXED);
    return newp;
}

unsigned packet_length(const struct Packet *p)
{
    unsigned packetlen = 0;
    for (unsigned i=0; i<p->header_count; i++) {
        unsigned len = p->headers[i].len;
        packetlen += len;
    }
    return packetlen;
}

struct Packet *serialize_packet(const struct Packet *p)
{
    struct Packet *ret = calloc_struct(Packet);
    *ret = *p;

    ret->buf = (unsigned char *)calloc(1, PACKET_BUF_LEN);
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
    ret->scratch_len = 0;
    ret->header_count = 0;

    ret->id = __atomic_fetch_add(&next_packet_id, 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&packet_count, 1, __ATOMIC_RELAXED);
    return ret;
}

bool packet_dummy(const struct Packet *p)
{
    return p->buf == dummybuf;
}

bool packet_identify_header(struct Packet *p, enum ProtocolID type, unsigned offset, unsigned len)
{
    if (p->header_count == PACKET_MAX_HEADER_NUM) {
        log_error("packet_identify_header: already at maximum header count");
        return false;
    }
    if (p->header_count > 0 && p->headers[p->header_count-1].start >= p->start + offset) {
        log_error("packet_identify_header: new start %u + %u is not larger than the previous %u",
                p->start, offset, p->headers[p->header_count-1].start);
        return false;
    }
    if (offset + len > p->len) {
        log_error("packet_identify_header: offset %u + len %u is larger than the packet %u",
                offset, len, p->len);
        return false;
    }
    struct PacketHeader *h = p->headers+p->header_count;
    h->type = type;
    h->start = p->start + offset;
    h->len = len;
    p->header_count++;
    return true;
}

// returns the start offset of the allocated space, or -1 on error
static int scratch_alloc(struct Packet *p, unsigned len)
{
    if (p->scratch_len + len >= PACKET_START_OFFSET) return -1;
    off_t ret = p->scratch_len;
    p->scratch_len += len;
    return ret;
}

//TODO static void scratch_free(struct Packet *p, unsigned char *start)

bool packet_add_header(struct Packet *p, unsigned idx, enum ProtocolID type, unsigned len)
{
    if (p->header_count == PACKET_MAX_HEADER_NUM) {
        //TODO can we prevent this in the config compiler?
        log_error("packet_add_header too many headers");
        return false;
    }
    if (idx > p->header_count) {
        //TODO if this happens, the config compiler is broken
        log_error("packet_add_header index too large %u > %u", idx, p->header_count);
        return false;
    }
    int start = scratch_alloc(p, len);
    if (start < 0) {
        log_error("packet_add_header out of scratch space");
        return false;
    }

    if (idx < p->header_count)
        memmove(p->headers+idx+1, p->headers+idx,
                (p->header_count-idx)*sizeof(struct PacketHeader));
    p->headers[idx].type = type;
    p->headers[idx].start = start;
    p->headers[idx].len = len;
    p->header_count++;
    return true;
}

void packet_del_header(struct Packet *p, unsigned idx)
{
    if (idx >= p->header_count) {
        //TODO can this happen?
        log_error("packet_del_header index too large %u > %u", idx, p->header_count);
        return;
    }

    if (idx < p->header_count)
        memmove(p->headers+idx, p->headers+idx+1,
                (p->header_count-idx-1)*sizeof(struct PacketHeader));
    p->header_count--;
}

void packet_clear_headers(struct Packet *p)
{
    p->header_count = 0;
    p->scratch_len = 0;
}

void packets_check_performance(void)
{
    if (packet_count > PACKET_COUNT_LIMIT * 0.9) {
        log_warning("SEVERE PERFORMANCE WARNING: too many packets in the system");

        // rate is limited by the caller
        struct JsonValue *js = json_object();

        json_object_insert(js, "warning", json_string("performance"));
        json_object_insert(js, "cause", json_string("too many packets in the system"));
        json_object_insert(js, "severity", json_string("high"));
        notification_push_event("send", NOTIF_WARNING, js);

    } else if (packet_count > PACKET_COUNT_LIMIT * 0.5) {
        log_warning("PERFORMANCE WARNING: too many packets in the system");

        // rate is limited by the caller
        struct JsonValue *js = json_object();
        json_object_insert(js, "warning", json_string("performance"));
        json_object_insert(js, "cause", json_string("too many packets in the system"));
        json_object_insert(js, "severity", json_string("medium"));
        notification_push_event("send", NOTIF_WARNING, js);

    }
}

void packet_logcat(struct Packet *p, const char *frmt, ...)
{
    if (!log_enabled_m(PACKETTRACE, PACKET))
        return;

    char msg[PACKET_LOG_BUF_SIZE - 1];
    va_list argp;

    if (p->logbuf[PACKET_LOG_BUF_SIZE - 2] != '\0')
        return;

    const size_t buflen = strlen(p->logbuf);

    va_start(argp, frmt);
    size_t n = vsnprintf(msg, sizeof(msg), frmt, argp);
    va_end(argp);

    strncpy(p->logbuf+buflen, msg, PACKET_LOG_BUF_SIZE-buflen-1);

    if (n + buflen >= PACKET_LOG_BUF_SIZE - 1)
        sprintf(&p->logbuf[PACKET_LOG_BUF_SIZE - 4], "...");
}

void packet_printlog(const struct Packet *p)
{
    log_packet_m(PACKETTRACE, "[id=%u oid=%u] %s", p->id, p->original_id, p->logbuf);
}

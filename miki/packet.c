
#include "packet.h"
#include "protocol.h"
#include "utils.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static unsigned packet_count = 0;
static unsigned char *dummybuf = NULL;

static void free_dummybuf(void) __attribute__((destructor));
static void free_dummybuf(void)
{
    free(dummybuf);
}

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

void packet_identify_header(struct Packet *p, int type, unsigned offset, unsigned len)
{
    if (p->header_count == PACKET_MAX_HEADER_NUM) {
        fprintf(stderr, "packet_identify_header: already at maximum header count\n");
        return;
    }
    if (p->header_count > 0 && p->headers[p->header_count-1].start > p->start + offset) {
        fprintf(stderr, "packet_identify_header: new offset % is smaller than the previous %u\n",
                offset, p->headers[p->header_count-1].start);
        return;
    }
    struct PacketHeader *h = p->headers+p->header_count;
    h->type = type;
    h->start = p->start + offset;
    h->len = len;
    p->header_count++;
    //TODO insert into the array sorted by offset
    //unsigned hindex = 0;

}

static off_t scratch_alloc(struct Packet *p, unsigned len)
{
    if (p->scratch_len + len >= PACKET_START_OFFSET) return -1;
    off_t ret = p->scratch_len;
    p->scratch_len += len;
    return ret;
}

//TODO static void scratch_free(struct Packet *p, unsigned char *start)

void packet_add_header(struct Packet *p, unsigned idx, int type, unsigned len)
{
    if (p->header_count == PACKET_MAX_HEADER_NUM) {
        //TODO error (can we prevent this in the config compiler?)
    }
    if (idx > p->header_count) {
        //TODO error (this should never happen though)
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

enum ProtocolFieldType packet_get_property_type(const char *name)
{
    if (strcmp(name, "seq") == 0) {
        return FT_TSNSEQ;
    } else if (strcmp(name, "tstamp") == 0) {
        return FT_TSNTSTAMP;
    } else if (strcmp(name, "recvtime") == 0) {
        return FT_TSNTSTAMP;
    } else {
        return FT_UNKNOWN;
    }
}

static void write_seq(void *state, struct Value *value, struct Packet *p)
{
    (void)state;
    memcpy(&p->sequence, value->value, 4);
}

static void write_tstamp(void *state, struct Value *value, struct Packet *p)
{
    (void)state;
    memcpy(&p->timestamp, value->value, 4);
}

value_consumer *packet_get_property_writer(const char *name, struct Value *source)
{
    if (source->bitoffset >0 || source->bitcount != 32) {
        return NULL;
    }

    if (strcmp(name, "seq") == 0) {
        return write_seq;
    } else if (strcmp(name, "tstamp") == 0) {
        return write_tstamp;
    } else {
        return NULL;
    }
}

static void read_seq(void *state, value_consumer *consumer, void *consumer_state, struct Packet *p)
{
    (void)state;
    struct Value v = {.value=&p->sequence, .bitoffset = 0, .bitcount = 32};
    consumer(consumer_state, &v, p);
}

static void read_tstamp(void *state, value_consumer *consumer, void *consumer_state, struct Packet *p)
{
    (void)state;
    struct Value v = {.value=&p->timestamp, .bitoffset = 0, .bitcount = 32};
    consumer(consumer_state, &v, p);
}

static void read_recvtime(void *state, value_consumer *consumer, void *consumer_state, struct Packet *p)
{
    (void)state;
    //TODO ez nem jó, egyrészt 20 bit másrészt htonl harmadrészt utilba kéne rakni
    //      util vagy conf_util?
    //      hol kell még használni?
    unsigned time = ((p->recv_time.tv_sec % 2) << 16) + p->recv_time.tv_nsec / 1000;
    struct Value v = {.value=&time, .bitoffset = 0, .bitcount = 32};
    consumer(consumer_state, &v, p);
}

value_producer *packet_get_property_reader(const char *name, struct Value *target)
{
    if (target->bitoffset >0 || target->bitcount != 32) {
        return NULL;
    }

    if (strcmp(name, "seq") == 0) {
        return read_seq;
    } else if (strcmp(name, "tstamp") == 0) {
        return read_tstamp;
    } else if (strcmp(name, "recvtime") == 0) {
        return read_recvtime;
    } else {
        return NULL;
    }
}


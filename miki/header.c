
#include "header.h"
#include "packet.h"

#include <string.h>
//TODO include this at more places?
#include <inttypes.h>

static void assign_bytes(void *state, struct Value *value, struct Packet *p)
{
    struct HeaderField *field = state;
    uint8_t *src = value->value;
    uint8_t *dst = p->buf + p->headers[field->header_idx].start + field->bitoffset/8;
    unsigned len = field->bitcount / 8;
    memcpy(dst, src, len);
}


value_consumer *get_assign_function(const struct HeaderField *target, const struct Value *source)
{
    if (source->bitcount > target->bitcount) {
        //TODO error
        return NULL;
    }
    if ((target->bitoffset % 8) == 0 && (target->bitcount % 8) == 0 && (source->bitoffset % 8) == 0) {
        return assign_bytes;
    }

    //TODO more variations:
    //      assign_bits for non-octet-based stuff
    //      special optimized cases may be useful for
    //          3bit  (priority)
    //          12bit (vlan id)
    //          20bit (mpls label)

    return NULL;
}

static void read_bytes(void *state, value_consumer *consumer, void *consumer_state, struct Packet *p)
{
    struct HeaderField *source = state;
    uint8_t *src = p->buf + p->headers[source->header_idx].start + source->bitoffset/8;
    struct Value val = {src, source->bitoffset, source->bitcount};
    consumer(consumer_state, &val, p);
}

value_producer *get_read_function(const struct Value *target, const struct HeaderField *source)
{
    if (source->bitcount > target->bitcount) {
        //TODO error
        return NULL;
    }
    if ((source->bitoffset % 8) == 0 && (source->bitcount % 8) == 0 && (target->bitoffset % 8) == 0) {
        return read_bytes;
    }

    //TODO more variations

    return NULL;
}

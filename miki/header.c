
#include "header.h"
#include "packet.h"

#include <string.h>
//TODO include this at more places?
#include <inttypes.h>

static void assign_bytes(struct Packet *p, struct HeaderField *field, struct HeaderValue *value)
{
    uint8_t *src = value->value;
    uint8_t *dst = p->buf + p->headers[field->header_idx].start + field->bitoffset/8;
    unsigned len = field->bitcount / 8;
    memcpy(dst, src, len);
}


field_assign *get_assign_function(const struct HeaderField *field, struct HeaderValue *source)
{
    if (source->bitcount > field->bitcount) {
        //TODO error
        return NULL;
    }
    if ((field->bitoffset % 8) == 0 && (field->bitcount % 8) == 0 && (source->bitoffset % 8) == 0) {
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

static void read_bytes(struct Packet *p, struct HeaderField *target, field_assign *assign, void *state)
{
    struct HeaderField *source = state;
    uint8_t buf[64];
    struct HeaderValue val = {buf, source->bitoffset, source->bitcount};

    uint8_t *src = p->buf + p->headers[source->header_idx].start + source->bitoffset/8;
    unsigned len = source->bitcount / 8;
    //TODO check that len < 64;
    memcpy(buf, src, len);
    assign(p, target, &val);
}

value_generator *get_read_function(const struct HeaderField *field)
{
    if ((field->bitoffset % 8) == 0 && (field->bitcount % 8) == 0) {
        return read_bytes;
    }

    //TODO more variations

    return NULL;
}

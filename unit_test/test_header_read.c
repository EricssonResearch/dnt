
#include "testing.h"

#include "header.h"
#include "packet.h"

TEST_INIT("Header Read");

struct ValidateState {
    struct HeaderField *field;
    struct Value *value;
};

static void validate(void *state, struct Value *value, struct Packet *p)
{
    struct ValidateState *vs = (struct ValidateState *)state;
    OK(value->bitcount == vs->field->bitcount, "bitcount %u %u",
            value->bitcount, vs->field->bitcount);
    OK(value->bitoffset % 8 == vs->field->bitoffset % 8, "bitoffset %u %u",
            value->bitoffset, vs->field->bitoffset);

    // this is what the reader pointed us to
    //  (value->value might not point to the beginning of the header)
    unsigned char *valuep = (unsigned char *)value->value + value->bitoffset/8;
    // this is what we wanted to read
    //  (header 0 starts at the beginning of the received packet)
    unsigned char *headerp = p->buf + p->start + vs->field->bitoffset/8;
    OK(valuep == headerp, "pointers %p %p", valuep, headerp);
}

static void test_read(void)
{
    struct Packet *p = new_packet(NULL);
    p->len = 8;
    packet_identify_header(p, PROTO_ID_PAYLOAD, 0, 8);

    for (unsigned bitcount=1; bitcount<=32; bitcount++) {
        for (unsigned src_offs=0; src_offs+bitcount<=64; src_offs++) {
            unsigned bo = src_offs % 8;
            for (unsigned dst_offs=bo; dst_offs+bitcount<=64; dst_offs+=8) {
                struct HeaderField field;
                field.bitoffset = src_offs;
                field.bitcount = bitcount;
                field.header_idx = 0;
                struct Value value;
                value.bitoffset = dst_offs;
                value.bitcount = bitcount;
                value.value = NULL;

                value_producer *reader = header_get_field_reader(&value, &field);
                OK(reader != NULL, "offsets %u %u should be compatible", src_offs, dst_offs);

                struct ValidateState vs = { &field, &value };
                reader(&field, validate, &vs, p);
            }
        }
    }

    delete_packet(p);
}

TEST_CASES = {
    {"read", test_read},
    {NULL, NULL}
};


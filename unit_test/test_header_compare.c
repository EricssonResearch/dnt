
#include "testing.h"

#include "header.h"
#include "log.h"
#include "packet.h"
#include "utils.h"

#include <string.h>

TEST_INIT("Header Compare");

// we test 1..32 bit fields with 0..32 bit offset -> buffer is 64 bits
struct TestData {
    unsigned char a[8];
    unsigned char b[8];
    unsigned a_offs;
    unsigned b_offs;
    unsigned bitcount;
    bool equal;
};

static struct TestData data[] = {
#include "header_compare.dat"
};
static const unsigned data_count = ARRAY_SIZE(data);

static void test_compare(void)
{
    struct Packet *p = new_packet(NULL);
    p->len = 8;
    packet_identify_header(p, PROTO_ID_PAYLOAD, 0, 8);

    for (unsigned i=0; i<data_count; i++) {
        struct HeaderField field;
        field.bitoffset = data[i].a_offs;
        field.bitcount = data[i].bitcount;
        field.header_idx = 0;
        struct Value value;
        value.bitoffset = data[i].b_offs;
        value.bitcount = data[i].bitcount;
        value.value = data[i].b;
        memcpy(p->buf+p->start, data[i].a, 8);

        value_comparator *cmp = header_get_field_comprator(&field, &value);
        OK_FATAL(cmp != NULL, "offsets %u %u should be compatible",
                data[i].a_offs, data[i].b_offs);

        bool b = cmp(&field, &value, p);
        OK(b == data[i].equal, "at %u values should be %s", i, data[i].equal?"equal":"different");
    }

    delete_packet(p);
}

TEST_CASES = {
    {"compare", test_compare},
    {NULL, NULL}
};


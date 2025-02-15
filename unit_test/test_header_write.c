
#include "testing.h"

#include "header.h"
#include "log.h"
#include "notification.h"
#include "packet.h"
#include "utils.h"

#include <string.h>

TEST_INIT("Header Write");

// XXX stubs for stuff that we depend on but don't need
bool notification_push_event(const char *source, NotificationLevel level, struct JsonValue *message)
    { (void)source; (void)level; (void)message; return false; }
bool notification_register_source(const char *name, notification_pull_fn *callback, void *self, unsigned period_ms)
    { (void)name; (void)callback; (void)self; (void)period_ms; return true; }
// XXX end stubs

// we test 1..32 bit fields with 0..32 bit offset -> buffer is 64 bits
struct TestData{
    unsigned char src[8];
    unsigned char dst[8];
    unsigned char expected[8];
    unsigned src_offs;
    unsigned dst_offs;
    unsigned bitcount;
};

static struct TestData data[] = {
#include "header_write.dat"
};
static const unsigned data_count = ARRAY_SIZE(data);

static void test_write(void)
{
    struct Packet *p = new_packet(NULL);
    p->len = 8;
    packet_identify_header(p, PROTO_ID_PAYLOAD, 0, 8);

    for (unsigned i=0; i<data_count; i++) {
        struct HeaderField field;
        field.bitoffset = data[i].dst_offs;
        field.bitcount = data[i].bitcount;
        field.header_idx = 0;
        struct Value value;
        value.bitoffset = data[i].src_offs;
        value.bitcount = data[i].bitcount;
        value.value = data[i].src;
        memcpy(p->buf+p->start, data[i].dst, 8);

        value_consumer *writer = header_get_field_writer(&field, &value);
        OK_FATAL(writer != NULL, "offsets %u %u should be compatible",
                data[i].src_offs, data[i].dst_offs);
        writer(&field, &value, p);

        OK(memcmp(p->buf+p->start, data[i].expected, 8) == 0,
                "at %u got %.2x %.2x %.2x %.2x %.2x %.2x %.2x %.2x", i,
                p->buf[p->start+0], p->buf[p->start+1],
                p->buf[p->start+2], p->buf[p->start+3],
                p->buf[p->start+4], p->buf[p->start+5],
                p->buf[p->start+6], p->buf[p->start+7]);
    }

    delete_packet(p);
}

TEST_CASES = {
    {"write", test_write},
    {NULL, NULL}
};


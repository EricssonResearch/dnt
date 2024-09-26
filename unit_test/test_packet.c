
#include "testing.h"

#include "packet.h"
#include "log.h"
#include "utils.h"

#include <string.h>

TEST_INIT("Packet");

static void fill_packet(struct Packet *p, unsigned offset, unsigned count)
{
    for (unsigned i=0; i<count; i++) p->buf[offset+i] = i;
    p->len = count;
}

static void test_create(void)
{
    log_set_level("PACKET", NONE);

    struct Packet *p = new_packet((struct Interface *)42);
    OK_FATAL(p != NULL, "have packet");
    OK_FATAL(p->buf != NULL, "have packet buffer");
    OK(!packet_dummy(p), "not dummy");
    OK(p->start == PACKET_START_OFFSET, "start");
    OK(p->len == 0, "len");
    OK(p->scratch_len == 0, "scratch empty");
    OK(p->header_count == 0, "no headers");
    OK(p->from == (struct Interface *)42, "from");
    OK(p->id == p->original_id, "id");

    OK(delete_packet(p) == NULL, "always returns null");

    struct Packet *pa[PACKET_COUNT_LIMIT*2];
    for (unsigned i=0; i<ARRAY_SIZE(pa); i++) {
        pa[i] = new_packet(NULL);
        OK_FATAL(pa[i] != NULL, "have packet");
        OK_FATAL(pa[i]->buf != NULL, "have packet buffer");
    }

    for (unsigned i=0; i<ARRAY_SIZE(pa); i++) {
        OK(packet_dummy(pa[i]) == (i >= PACKET_COUNT_LIMIT), "dummy if over the limit");
        OK(pa[i]->id == pa[i]->original_id, "id");
        if (i > 0) {
            OK(pa[i]->id > pa[i-1]->id, "id different");
        }
    }

    for (unsigned i=0; i<ARRAY_SIZE(pa); i++) {
        OK_FATAL(delete_packet(pa[i]) == NULL, "always returns null");
    }

    p = new_packet((struct Interface *)42);
    OK(!packet_dummy(p), "not dummy");
    OK(delete_packet(p) == NULL, "always returns null");
}

static void test_identify(void)
{
    struct Packet *p = new_packet((struct Interface *)42);
    OK_FATAL(p != NULL, "have packet");
    OK_FATAL(p->buf != NULL, "have packet buffer");
    fill_packet(p, p->start, 256);

    // a fairly normal proto stack
    OK(packet_identify_header(p, PROTO_ID_ETH, 0, 14) == true, "eth");
    OK(packet_identify_header(p, PROTO_ID_CVLAN, 14, 4) == true, "cvlan");
    OK(packet_identify_header(p, PROTO_ID_IPv6, 18, 40) == true, "ipv6");
    OK(packet_identify_header(p, PROTO_ID_PAYLOAD, 58, 198) == true, "payload");
    OK(p->header_count == 4, "header_count %u", p->header_count);
    OK(p->start == PACKET_START_OFFSET, "start");
    OK(p->len == 256, "len %u", p->len);
    OK(p->scratch_len == 0, "scratch empty");
    packet_clear_headers(p);
    OK(p->header_count == 0, "header_count %u", p->header_count);
    OK(p->start == PACKET_START_OFFSET, "start");
    OK(p->len == 256, "len %u", p->len);
    OK(p->scratch_len == 0, "scratch empty");

    // header length can be anything
    OK(packet_identify_header(p, PROTO_ID_IPv6, 0, 1) == true, "identify");
    OK(packet_identify_header(p, PROTO_ID_IPv6, 1, 1) == true, "identify");
    OK(packet_identify_header(p, PROTO_ID_IPv6, 2, 1) == true, "identify");
    OK(packet_identify_header(p, PROTO_ID_IPv6, 3, 1) == true, "identify");
    OK(packet_identify_header(p, PROTO_ID_IPv6, 4, 1) == true, "identify");
    OK(packet_identify_header(p, PROTO_ID_IPv6, 5, 1) == true, "identify");
    OK(p->header_count == 6, "header_count %u", p->header_count);
    OK(p->start == PACKET_START_OFFSET, "start");
    OK(p->len == 256, "len %u", p->len);
    OK(p->scratch_len == 0, "scratch empty");
    packet_clear_headers(p);
    OK(p->header_count == 0, "header_count %u", p->header_count);
    OK(p->start == PACKET_START_OFFSET, "start");
    OK(p->len == 256, "len %u", p->len);
    OK(p->scratch_len == 0, "scratch empty");

    // offsets can have gaps
    //TODO why do we allow this?
    OK(packet_identify_header(p, PROTO_ID_IPv6,  0, 1) == true, "identify");
    OK(packet_identify_header(p, PROTO_ID_IPv6, 10, 1) == true, "identify");
    OK(packet_identify_header(p, PROTO_ID_IPv6, 20, 1) == true, "identify");
    OK(packet_identify_header(p, PROTO_ID_IPv6, 30, 1) == true, "identify");
    OK(packet_identify_header(p, PROTO_ID_IPv6, 40, 1) == true, "identify");
    OK(packet_identify_header(p, PROTO_ID_IPv6, 50, 1) == true, "identify");
    OK(p->header_count == 6, "header_count %u", p->header_count);
    OK(p->start == PACKET_START_OFFSET, "start");
    OK(p->len == 256, "len %u", p->len);
    OK(p->scratch_len == 0, "scratch empty");
    packet_clear_headers(p);
    OK(p->header_count == 0, "header_count %u", p->header_count);
    OK(p->start == PACKET_START_OFFSET, "start");
    OK(p->len == 256, "len %u", p->len);
    OK(p->scratch_len == 0, "scratch empty");

    // offsets can overlap
    //TODO why do we allow this?
    OK(packet_identify_header(p, PROTO_ID_IPv6,  0, 20) == true, "identify");
    OK(packet_identify_header(p, PROTO_ID_IPv6, 10, 20) == true, "identify");
    OK(packet_identify_header(p, PROTO_ID_IPv6, 20, 20) == true, "identify");
    OK(packet_identify_header(p, PROTO_ID_IPv6, 30, 20) == true, "identify");
    OK(packet_identify_header(p, PROTO_ID_IPv6, 40, 20) == true, "identify");
    OK(packet_identify_header(p, PROTO_ID_IPv6, 50, 20) == true, "identify");
    OK(p->header_count == 6, "header_count %u", p->header_count);
    OK(p->start == PACKET_START_OFFSET, "start");
    OK(p->len == 256, "len %u", p->len);
    OK(p->scratch_len == 0, "scratch empty");
    packet_clear_headers(p);
    OK(p->header_count == 0, "header_count %u", p->header_count);
    OK(p->start == PACKET_START_OFFSET, "start");
    OK(p->len == 256, "len %u", p->len);
    OK(p->scratch_len == 0, "scratch empty");

    // length overflow
    OK(packet_identify_header(p, PROTO_ID_IPv6, 0, 257) == false, "too long");
    OK(packet_identify_header(p, PROTO_ID_IPv6,  0, 200) == true, "identify");
    OK(packet_identify_header(p, PROTO_ID_IPv6, 200, 57) == false, "too long");
    OK(p->header_count == 1, "header_count %u", p->header_count);
    packet_clear_headers(p);
    OK(p->header_count == 0, "header_count %u", p->header_count);

    // header count overflow
    for (unsigned i=0; i<PACKET_MAX_HEADER_NUM; i++) {
        OK(packet_identify_header(p, PROTO_ID_IPv6, i, 1) == true, "identify");
    }
    OK(p->header_count == PACKET_MAX_HEADER_NUM, "header_count %u", p->header_count);
    OK(packet_identify_header(p, PROTO_ID_IPv6, PACKET_MAX_HEADER_NUM, 1) == false, "too many");
    OK(p->header_count == PACKET_MAX_HEADER_NUM, "header_count %u", p->header_count);
    packet_clear_headers(p);
    OK(p->header_count == 0, "header_count %u", p->header_count);

    // not increasing offset
    //TODO why do we forbid this?
    OK(packet_identify_header(p, PROTO_ID_IPv6,  0, 10) == true, "identify");
    OK(packet_identify_header(p, PROTO_ID_IPv6, 0, 20) == false, "same offset");
    OK(packet_identify_header(p, PROTO_ID_IPv6,  10, 10) == true, "identify");
    OK(packet_identify_header(p, PROTO_ID_IPv6, 9, 20) == false, "decreasing offset");
    OK(p->header_count == 2, "header_count %u", p->header_count);

    // check that packet buffer is still intact
    OK(p->start == PACKET_START_OFFSET, "start");
    OK_FATAL(p->buf != NULL, "have packet buffer");
    for (unsigned i=0; i<PACKET_START_OFFSET; i++) {
        OK(p->buf[i] == 0, "scratch space was changed %u %u", i, p->buf[i]);
    }
    for (unsigned i=0; i<p->len; i++) {
        OK(p->buf[PACKET_START_OFFSET+i] == i, "data was changed %u %u", i, p->buf[PACKET_START_OFFSET+i]);
    }
    //TODO check the bytes after the used part?

    OK(delete_packet(p) == NULL, "always returns null");
}

static void test_add_del(void)
{
    struct Packet *p = new_packet((struct Interface *)42);
    OK_FATAL(p != NULL, "have packet");
    OK_FATAL(p->buf != NULL, "have packet buffer");
    fill_packet(p, p->start, 256); // some data in the receive area
    fill_packet(p, 0, 256); // some data in the scratch space

    // start with a fairly normal proto stack
    // the starting offset is there so that the corresponding packet data is 128...255
    OK(packet_identify_header(p, PROTO_ID_ETH, 128, 14) == true, "eth");
    OK(packet_identify_header(p, PROTO_ID_CVLAN, 128+14, 4) == true, "cvlan");
    OK(packet_identify_header(p, PROTO_ID_IPv6, 128+18, 40) == true, "ipv6");
    OK(packet_identify_header(p, PROTO_ID_PAYLOAD, 128+58, 70) == true, "payload");

    // add things
    // note: we don't get a return value, because
    //       validation happened in the config compiler
    OK(p->header_count == 4, "header_count %u", p->header_count);
    packet_add_header(p, 2, PROTO_ID_SVLAN, 4); // middle
    OK(p->header_count == 5, "header_count %u", p->header_count);
    packet_add_header(p, 5, PROTO_ID_ARP, 10); // end
    OK(p->header_count == 6, "header_count %u", p->header_count);
    packet_add_header(p, 0, PROTO_ID_IPv4, 20); // beginning
    OK(p->header_count == 7, "header_count %u", p->header_count);
    packet_add_header(p, 8, PROTO_ID_MPLS, 4); // this is invalid
    OK(p->header_count == 7, "header_count %u", p->header_count);

    OK(p->start == PACKET_START_OFFSET, "start");
    OK(p->len == 256, "len %u", p->len);
    OK(p->scratch_len == 20+10+4, "scratch allocation %u", p->scratch_len);

    OK(p->headers[0].type == PROTO_ID_IPv4, "header type %d", p->headers[0].type);
    OK(p->headers[1].type == PROTO_ID_ETH, "header type %d", p->headers[1].type);
    OK(p->headers[2].type == PROTO_ID_CVLAN, "header type %d", p->headers[2].type);
    OK(p->headers[3].type == PROTO_ID_SVLAN, "header type %d", p->headers[3].type);
    OK(p->headers[4].type == PROTO_ID_IPv6, "header type %d", p->headers[4].type);
    OK(p->headers[5].type == PROTO_ID_PAYLOAD, "header type %d", p->headers[5].type);
    OK(p->headers[6].type == PROTO_ID_ARP, "header type %d", p->headers[6].type);

    OK(p->headers[0].start == 4+10, "header start %u", p->headers[0].start);
    OK(p->headers[1].start == PACKET_START_OFFSET+128, "header start %u", p->headers[1].start);
    OK(p->headers[2].start == PACKET_START_OFFSET+128+14, "header start %u", p->headers[2].start);
    OK(p->headers[3].start == 0, "header start %u", p->headers[3].start);
    OK(p->headers[4].start == PACKET_START_OFFSET+128+18, "header start %u", p->headers[4].start);
    OK(p->headers[5].start == PACKET_START_OFFSET+128+58, "header start %u", p->headers[5].start);
    OK(p->headers[6].start == 4, "header start %u", p->headers[6].start);

    OK(p->headers[0].len == 20, "header len %u", p->headers[0].len);
    OK(p->headers[1].len == 14, "header len %u", p->headers[1].len);
    OK(p->headers[2].len == 4, "header len %u", p->headers[2].len);
    OK(p->headers[3].len == 4, "header len %u", p->headers[3].len);
    OK(p->headers[4].len == 40, "header len %u", p->headers[4].len);
    OK(p->headers[5].len == 70, "header len %u", p->headers[5].len);
    OK(p->headers[6].len == 10, "header len %u", p->headers[6].len);

    for (unsigned i=0; i<256; i++) {
        OK(p->buf[i] == i, "scratch space was changed %u %u", i, p->buf[i]);
        OK(p->buf[PACKET_START_OFFSET+i] == i, "data was changed %u %u", i, p->buf[PACKET_START_OFFSET+i]);
    }

    // del things
    OK(p->header_count == 7, "header_count %u", p->header_count);
    packet_del_header(p, 0); // IPv4
    OK(p->header_count == 6, "header_count %u", p->header_count);
    packet_del_header(p, 0); // ETH
    OK(p->header_count == 5, "header_count %u", p->header_count);
    packet_del_header(p, 1); // SVLAN
    OK(p->header_count == 4, "header_count %u", p->header_count);
    packet_del_header(p, 1); // IPv6
    OK(p->header_count == 3, "header_count %u", p->header_count);
    packet_del_header(p, 2); // ARP
    OK(p->header_count == 2, "header_count %u", p->header_count);
    packet_del_header(p, 1); // PAYLOAD
    OK(p->header_count == 1, "header_count %u", p->header_count);
    packet_del_header(p, 1); // invalid
    OK(p->header_count == 1, "header_count %u", p->header_count);

    OK(p->start == PACKET_START_OFFSET, "start");
    OK(p->len == 256, "len %u", p->len);
    // note: deleting a header doesn't deallocate the scratch space
    OK(p->scratch_len == 20+10+4, "scratch allocation %u", p->scratch_len);
    OK(p->headers[0].type == PROTO_ID_CVLAN, "header type %d", p->headers[0].type);

    // add again
    packet_add_header(p, 0, PROTO_ID_ETH, 14); // beginning
    packet_add_header(p, 2, PROTO_ID_MPLS, 4); // end

    OK(p->header_count == 3, "header_count %u", p->header_count);
    OK(p->start == PACKET_START_OFFSET, "start");
    OK(p->len == 256, "len %u", p->len);
    OK(p->scratch_len == 20+10+4+14+4, "scratch allocation %u", p->scratch_len);

    OK(p->headers[0].type == PROTO_ID_ETH, "header type %d", p->headers[0].type);
    OK(p->headers[1].type == PROTO_ID_CVLAN, "header type %d", p->headers[1].type);
    OK(p->headers[2].type == PROTO_ID_MPLS, "header type %d", p->headers[2].type);

    OK(p->headers[0].start == 20+4+10, "header start %u", p->headers[0].start);
    OK(p->headers[1].start == PACKET_START_OFFSET+128+14, "header start %u", p->headers[1].start);
    OK(p->headers[2].start == 20+4+10+14, "header start %u", p->headers[2].start);

    OK(p->headers[0].len == 14, "header len %u", p->headers[0].len);
    OK(p->headers[1].len == 4, "header len %u", p->headers[1].len);
    OK(p->headers[2].len == 4, "header len %u", p->headers[2].len);

    for (unsigned i=0; i<256; i++) {
        OK(p->buf[i] == i, "scratch space was changed %u %u", i, p->buf[i]);
        OK(p->buf[PACKET_START_OFFSET+i] == i, "data was changed %u %u", i, p->buf[PACKET_START_OFFSET+i]);
    }

    OK(delete_packet(p) == NULL, "always returns null");
}

static void test_copy(void)
{
    //TODO test that the metadata fields and timestamps are copied properly

    struct Packet *p = new_packet((struct Interface *)42);
    OK_FATAL(p != NULL, "have packet");
    OK_FATAL(p->buf != NULL, "have packet buffer");
    OK(p->id == p->original_id, "id");
    fill_packet(p, p->start, 256); // some data in the receive area
    fill_packet(p, 0, 256); // some data in the scratch space

    // start with a fairly normal proto stack
    // the starting offset is there so that the corresponding packet data is 128...255
    OK(packet_identify_header(p, PROTO_ID_ETH, 128, 14) == true, "eth");
    OK(packet_identify_header(p, PROTO_ID_CVLAN, 128+14, 4) == true, "cvlan");
    OK(packet_identify_header(p, PROTO_ID_IPv6, 128+18, 40) == true, "ipv6");
    OK(packet_identify_header(p, PROTO_ID_PAYLOAD, 128+58, 70) == true, "payload");
    packet_add_header(p, 4, PROTO_ID_ARP, 10); // end
    packet_add_header(p, 0, PROTO_ID_IPv4, 20); // beginning
    packet_add_header(p, 3, PROTO_ID_SVLAN, 4); // middle

    OK(p->header_count == 7, "header_count %u", p->header_count);
    OK(p->start == PACKET_START_OFFSET, "start");
    OK(p->len == 256, "len %u", p->len);
    OK(p->scratch_len == 20+10+4, "scratch allocation %u", p->scratch_len);

    OK(p->headers[0].type == PROTO_ID_IPv4, "header type %d", p->headers[0].type);
    OK(p->headers[1].type == PROTO_ID_ETH, "header type %d", p->headers[1].type);
    OK(p->headers[2].type == PROTO_ID_CVLAN, "header type %d", p->headers[2].type);
    OK(p->headers[3].type == PROTO_ID_SVLAN, "header type %d", p->headers[3].type);
    OK(p->headers[4].type == PROTO_ID_IPv6, "header type %d", p->headers[4].type);
    OK(p->headers[5].type == PROTO_ID_PAYLOAD, "header type %d", p->headers[5].type);
    OK(p->headers[6].type == PROTO_ID_ARP, "header type %d", p->headers[6].type);

    // copy makes an identical copy (used by replicate action)
    struct Packet *cp = copy_packet(p);
    OK_FATAL(cp != NULL, "have copy");
    OK_FATAL(cp->buf != NULL, "have buffer");
    OK(cp->buf != p->buf, "different buffer");
    OK(cp->id > p->id, "id %u %u", cp->id, p->id);
    OK(cp->original_id == p->original_id, "original id %u %u", cp->original_id, p->original_id);
    OK(cp->start == p->start, "start %u %u", cp->start, p->start);
    OK(cp->len == p->len, "len %u %u", cp->len, p->len);
    OK(cp->scratch_len == p->scratch_len, "scratch_len %u %u", cp->scratch_len, p->scratch_len);
    OK(cp->from == p->from, "from %p %p", cp->from, p->from);
    OK(memcmp(cp->buf, p->buf, p->start+p->len) == 0, "same buffer contents"); // only compare the used portion
    OK(cp->header_count == p->header_count, "header_count %u %u", cp->header_count, p->header_count);
    for (unsigned i=0; i<p->header_count; i++) {
        OK(memcmp(cp->headers+i, p->headers+i, sizeof(struct PacketHeader)) == 0, "header %u different", i);
    }

    struct Packet *ccp = copy_packet(cp);
    OK_FATAL(ccp != NULL, "have copy");
    OK_FATAL(ccp->buf != NULL, "have buffer");
    OK(ccp->buf != p->buf, "different buffer");
    OK(ccp->id > p->id, "id %u %u", ccp->id, p->id);
    OK(ccp->id > cp->id, "id %u %u", ccp->id, cp->id);
    OK(ccp->original_id == p->original_id, "original id %u %u", ccp->original_id, p->original_id);
    OK(ccp->start == p->start, "start %u %u", ccp->start, p->start);
    OK(ccp->len == p->len, "len %u %u", ccp->len, p->len);
    OK(ccp->scratch_len == p->scratch_len, "scratch_len %u %u", ccp->scratch_len, p->scratch_len);
    OK(ccp->from == p->from, "from %p %p", ccp->from, p->from);
    OK(memcmp(ccp->buf, p->buf, p->start+p->len) == 0, "same buffer contents"); // only compare the used portion
    OK(ccp->header_count == p->header_count, "header_count %u %u", ccp->header_count, p->header_count);
    for (unsigned i=0; i<p->header_count; i++) {
        OK(memcmp(ccp->headers+i, p->headers+i, sizeof(struct PacketHeader)) == 0, "header %u different", i);
    }

    // serialize creates a packet that looks like it was received on the wire (used by if_internal)
    struct Packet *sp = serialize_packet(p);
    OK_FATAL(sp != NULL, "have copy");
    OK_FATAL(sp->buf != NULL, "have buffer");
    OK(sp->buf != p->buf, "different buffer");
    OK(sp->id > p->id, "id %u %u", sp->id, p->id);
    OK(sp->original_id == p->original_id, "original id %u %u", sp->original_id, p->original_id);
    OK(sp->start == PACKET_START_OFFSET, "start %u", sp->start);
    OK(sp->len == 14+4+40+70+10+20+4, "len %u", sp->len);
    OK(sp->scratch_len == 0, "scratch_len %u", sp->scratch_len);
    OK(sp->from == p->from, "from %p %p", sp->from, p->from);
    OK(sp->header_count == 0, "header_count %u", sp->header_count);
    // check buffer against the original headers
    unsigned su = 0;
    for (unsigned i=0; i<p->header_count; i++) {
        for (unsigned j=0; j<p->headers[i].len; j++) {
            OK(sp->buf[PACKET_START_OFFSET+su] == p->buf[p->headers[i].start+j], "byte %u differs %u %u", su,
                    sp->buf[PACKET_START_OFFSET+su], p->buf[p->headers[i].start+j]);
            su++;
        }
    }
    OK(su == sp->len, "su %u len %u", su, sp->len);

    OK(delete_packet(p) == NULL, "always returns null");
    OK(delete_packet(sp) == NULL, "always returns null");
    OK(delete_packet(cp) == NULL, "always returns null");
    OK(delete_packet(ccp) == NULL, "always returns null");
}

static void test_logbuf(void)
{
    struct Packet *p = new_packet((struct Interface *)42);
    OK_FATAL(p != NULL, "have packet");
    OK_FATAL(p->buf != NULL, "have packet buffer");

    packet_logcat(p, "by default the packettrace is disabled");
    log_set_level("PACKETTRACE", PACKET);
    packet_logcat(p, "now %s", "we");
    packet_logcat(p, " are");
    packet_logcat(p, " %ding", 1099);

    OK(strcmp(p->logbuf, "now we are 1099ing") == 0, "log '%s'", p->logbuf);

    OK(delete_packet(p) == NULL, "always returns null");
}

TEST_CASES = {
    {"create", test_create},
    {"identify header", test_identify},
    {"add/del header", test_add_del},
    {"copy", test_copy},
    {"logbuf", test_logbuf},
    {NULL, NULL}
};

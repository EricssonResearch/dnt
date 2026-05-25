
#include "testing.h"

#include "conf_utils.h"
#include "log.h"
#include "utils.h"
#include "value.h"

#include <string.h>
#include <stdlib.h>

TEST_INIT("Conf Utils");

struct StageVerifyState {
    const char *input;
    const char **output;
    unsigned expected_count;
    unsigned count;
};

static bool stage_verify(char *str, void *userdata)
{
    struct StageVerifyState *svs = (struct StageVerifyState *)userdata;
    OK(svs->count < svs->expected_count, "input '%s' count %u expected %u",
            svs->input, svs->count, svs->expected_count);
    OK(strcmp(str, svs->output[svs->count]) == 0, "input '%s' stage %u mismatch '%s' '%s'",
            svs->input, svs->count, str, svs->output[svs->count]);
    svs->count++;
    return true;
}

static bool stage_interrupt(char *str, void *userdata)
{
    (void)str;
    unsigned *i = (unsigned *)userdata;
    return ++(*i) < 2;
}

static void test_foreach_stages(void)
{
#define EXPECTED(...) __VA_ARGS__
#define TRY(_input, _output)                                                                \
    do {                                                                                    \
        char *in_ = strdup(_input);                                                         \
        const char *out_[] = _output;                                                       \
        struct StageVerifyState svs = {_input, out_, ARRAY_SIZE(out_), 0};                  \
        OK(foreach_stages(in_, stage_verify, &svs) == true, "not interrupted");             \
        OK(svs.count == svs.expected_count, "count %u %u", svs.count, svs.expected_count);  \
        free(in_);                                                                          \
    } while (0)

    TRY("a,b,c,d", EXPECTED({"a", "b", "c", "d"}));
    TRY("  a  ,  \tb  ,  c\t ,  d  \n", EXPECTED({"a  ", "b  ", "c\t ", "d  \n"}));
    TRY(" a  b  c  d ", EXPECTED({"a  b  c  d "}));
    TRY("            ", EXPECTED({}));
    TRY("", EXPECTED({}));
    TRY("\n", EXPECTED({}));
    TRY("more words per stage , also works fine , and no problem ",
            EXPECTED({"more words per stage ", "also works fine ", "and no problem "}));
    TRY("árvíztűrő, tükörfúrógép", EXPECTED({"árvíztűrő", "tükörfúrógép"}));
    // libc doesn't consider Ideographic Space to be whitespace
    // and Ideographic Comma is not a real comma
    TRY("　北　,　西　、　南　,　東　", EXPECTED({"　北　", "　西　、　南　", "　東　"}));

#undef EXPECTED
#undef TRY

    unsigned cnt = 0;
    char *inp = strdup("a,b,c,d,e,f");
    OK(foreach_stages(inp, stage_interrupt, &cnt) == false, "not interrupted");
    OK(cnt == 2, "cnt %u", cnt);
    free(inp);
}

struct TokenVerifyState {
    const char *input;
    const char **output;
    unsigned expected_count;
    unsigned count;
};

static bool token_verify(char *str, void *userdata)
{
    struct TokenVerifyState *tvs = (struct TokenVerifyState *)userdata;
    OK(tvs->count < tvs->expected_count, "input '%s' count %u expected %u",
            tvs->input, tvs->count, tvs->expected_count);
    OK(strcmp(str, tvs->output[tvs->count]) == 0, "input '%s' stage %u mismatch '%s' '%s'",
            tvs->input, tvs->count, str, tvs->output[tvs->count]);
    tvs->count++;
    return true;
}

static bool token_interrupt(char *str, void *userdata)
{
    (void)str;
    unsigned *i = (unsigned *)userdata;
    return ++(*i) < 2;
}

static void test_foreach_tokens(void)
{
#define EXPECTED(...) __VA_ARGS__
#define TRY(_input, _output)                                                                \
    do {                                                                                    \
        char *in_ = strdup(_input);                                                         \
        const char *out_[] = _output;                                                       \
        struct TokenVerifyState tvs = {_input, out_, ARRAY_SIZE(out_), 0};                  \
        OK(foreach_tokens(in_, token_verify, &tvs) == true, "not interrupted");             \
        OK(tvs.count == tvs.expected_count, "count %u %u", tvs.count, tvs.expected_count);  \
        free(in_);                                                                          \
    } while (0)

    TRY("a,b,c,d", EXPECTED({"a,b,c,d"}));
    TRY(" a  b  c  d ", EXPECTED({"a", "b", "c", "d"}));
    TRY("  a  ,  \tb  ,  c\t ,  d  \n", EXPECTED({"a", ",", "b", ",", "c", ",", "d"}));
    TRY("            ", EXPECTED({}));
    TRY("", EXPECTED({}));
    TRY("\n", EXPECTED({}));
    TRY("longer tokens are also okay", EXPECTED({"longer", "tokens", "are", "also", "okay"}));
    TRY("árvíztűrő tükörfúrógép", EXPECTED({"árvíztűrő", "tükörfúrógép"}));
    // there is a regular space in the middle
    TRY("　北　　西　 　南　　東　", EXPECTED({"　北　　西　", "　南　　東　"}));

#undef EXPECTED
#undef TRY

    unsigned cnt = 0;
    char *inp = strdup("a b c d e f");
    OK(foreach_tokens(inp, token_interrupt, &cnt) == false, "not interrupted");
    OK(cnt == 2, "cnt %u", cnt);
    free(inp);
}

static void test_parse_assignment(void)
{
#define GOOD(_input, _key, _val)                                                            \
    do {                                                                                    \
        char *in_ = strdup(_input);                                                         \
        char *k_=NULL, *v_=NULL;                                                            \
        OK(parse_assignment(in_, &k_, &v_) == true, "string '%s' should be valid", _input); \
        OK_FATAL(k_ != NULL && v_ != NULL, "no result");                                    \
        OK(strcmp(k_, _key) == 0, "key mismatch '%s' '%s'", k_, _key);                      \
        OK(strcmp(v_, _val) == 0, "val mismatch '%s' '%s'", v_, _val);                      \
        free(in_);                                                                          \
    } while (0)
#define BAD(_input)                                                                             \
    do {                                                                                        \
        char *in_ = strdup(_input);                                                             \
        char *k_, *v_;                                                                          \
        OK(parse_assignment(in_, &k_, &v_) == false, "string '%s' should be invalid", _input);  \
        free(in_);                                                                              \
    } while (0)

    GOOD("a=b", "a", "b");
    GOOD("árvíztűrő=tükörfúrógép", "árvíztűrő", "tükörfúrógép");
    GOOD("a = b", "a ", " b");
    GOOD(" a = b ", " a ", " b ");
    GOOD("a, b = c, d ", "a, b ", " c, d ");
    GOOD("鍵=何も", "鍵", "何も");
    GOOD("= b", "", " b");
    GOOD("=", "", "");
    BAD("ab");
    BAD("a b");
    BAD("");
    BAD("    ");
    BAD("a = b= c");
    BAD("==");
    BAD("= =");

#undef GOOD
#undef BAD
}

static void test_parse_fieldname(void)
{
#define GOOD(_input, _header, _field)                                                       \
    do {                                                                                    \
        char *in_ = strdup(_input);                                                         \
        char *h_=NULL, *f_=NULL;                                                            \
        OK(parse_fieldname(in_, &h_, &f_) == true, "string '%s' should be valid", _input);  \
        OK(strcmp(h_, _header) == 0, "header mismatch '%s' '%s'", h_, _header);             \
        OK(strcmp(f_, _field) == 0, "field mismatch '%s' '%s'", f_, _field);                \
        free(in_);                                                                          \
    } while (0)
#define BAD(_input)                                                                             \
    do {                                                                                        \
        char *in_ = strdup(_input);                                                             \
        char *h_, *f_;                                                                          \
        OK(parse_fieldname(in_, &h_, &f_) == false, "string '%s' should be invalid", _input);   \
        free(in_);                                                                              \
    } while (0)

    GOOD("header.field", "header", "field");
    GOOD("header . field", "header ", " field");
    GOOD(" header . field ", " header ", " field ");
    GOOD("header.", "header", "");
    GOOD(".field", "", "field");
    GOOD(".", "", "");
    GOOD("árvíztűrő.tükörfúrógép", "árvíztűrő", "tükörfúrógép");
    GOOD("頭.足", "頭", "足");
    BAD("nofield");
    BAD("no field");
    BAD("");
    BAD("h.f.g");
    BAD("..");
    BAD(". .");

#undef GOOD
#undef BAD
}

static void test_header_type_from_name(void)
{
#define GOOD(_input, _type) \
    do { \
        char *type_ = header_type_from_name(_input); \
        OK_FATAL(type_ != NULL, "for '%s' type is null", _input);\
        OK(strcmp(type_, _type) == 0, "for '%s' type '%s' instead of '%s'", \
                _input, type_, _type); \
        free(type_); \
    } while (0)

    //note: none of our header types contain '_'
    GOOD("type", "type");
    GOOD("type_something", "type");
    GOOD("type_something_more", "type");
    GOOD("type__more", "type");
    GOOD("type_", "type");
    GOOD("_", "");
    GOOD("", "");
    GOOD("árvíztűrő_tükörfúrógép", "árvíztűrő");

#undef GOOD
}

static void test_read_boolean(void)
{
    OK(read_boolean("0") == 0, "0");
    OK(read_boolean("1") == 1, "1");
    OK(read_boolean("false") == 0, "0");
    OK(read_boolean("true") == 1, "1");
    OK(read_boolean("no") == 0, "0");
    OK(read_boolean("yes") == 1, "1");
    OK(read_boolean("00") == -1, "invalid");
    OK(read_boolean("0x0") == -1, "invalid");
    OK(read_boolean("") == -1, "invalid");
    OK(read_boolean("FALSE") == -1, "invalid");
    OK(read_boolean("yess") == -1, "invalid");
    OK(read_boolean("nooooo") == -1, "invalid");
    OK(read_boolean("on") == -1, "invalid");
    OK(read_boolean("off") == -1, "invalid");
    OK(read_boolean("igen") == -1, "invalid");
    OK(read_boolean("nem") == -1, "invalid");
}

struct ConstantTestData {
    unsigned char val[10];
    uint64_t num;
    unsigned offs;
    unsigned bitcount;
};

static struct ConstantTestData data[] = {
#include "conf_utils.dat"
};
static const unsigned data_count = ARRAY_SIZE(data);

static void test_prepare_constant_number(void)
{
    for (unsigned i=0; i<data_count; i++) {
        struct Value value;
        value.bitoffset = data[i].offs;
        value.bitcount = data[i].bitcount;
        value.value = NULL;
        OK(prepare_constant_number(&value, data[i].num) == true, "failed %u", i);
        OK_FATAL(value.value != NULL, "have value");
        OK(value.bitcount == data[i].bitcount, "bitcounts %u %u", value.bitcount, data[i].bitcount);
        OK((value.bitoffset % 8) == (data[i].offs % 8), "offsets %u %u", value.bitoffset, data[i].offs);

        unsigned len = DIVCEIL(value.bitoffset+value.bitcount, 8);
        OK(memcmp(value.value, data[i].val, len) == 0, "different %u len %u", i, len);
        free(value.value);
    }

    struct Value inval;
    for (unsigned bitcount=1; bitcount<64; bitcount++) {
        inval.bitcount = bitcount;
        uint64_t num = 1lu<<bitcount;
        for (unsigned bitoffset=0; bitoffset<=80-bitcount; bitoffset++) {
            inval.bitoffset = bitoffset;
            OK(prepare_constant_number(&inval, num) == false, "num 0x%lx should exceed %u bits", num, bitcount);
        }
    }
}

static void test_read_constant(void)
{
    log_set_level("CONFIG", LOGGING_NONE);
    struct Value val;
    unsigned char *buf;

    // the Value normally comes from the left-hand side of the match/edit expression
    // so read_constant() can trust its bitoffset/bitcount

    // PFTYPE_UNKNOWN
    OK(read_constant(&val, PROTO_ID_ETH, PFTYPE_UNKNOWN, "0") == false, "unknown should be rejected");


    // PFTYPE_NUMBER
    val.bitoffset = 3;
    val.bitcount = 1;
    OK(read_constant(&val, PROTO_ID_ETH, PFTYPE_NUMBER, "0") == true, "should be accepted");
    OK_FATAL(val.value != NULL, "have buffer");
    buf = (unsigned char *)val.value;
    OK(buf[0] == 0x00, "value 0x%x", buf[0]);
    free(val.value); val.value = NULL;
    OK(read_constant(&val, PROTO_ID_ETH, PFTYPE_NUMBER, "1") == true, "should be accepted");
    OK_FATAL(val.value != NULL, "have buffer");
    buf = (unsigned char *)val.value;
    OK(buf[0] == 0x10, "value 0x%x", buf[0]);
    free(val.value); val.value = NULL;
    OK(read_constant(&val, PROTO_ID_ETH, PFTYPE_NUMBER, "false") == true, "should be accepted");
    OK_FATAL(val.value != NULL, "have buffer");
    buf = (unsigned char *)val.value;
    OK(buf[0] == 0x00, "value 0x%x", buf[0]);
    free(val.value); val.value = NULL;
    OK(read_constant(&val, PROTO_ID_ETH, PFTYPE_NUMBER, "true") == true, "should be accepted");
    OK_FATAL(val.value != NULL, "have buffer");
    buf = (unsigned char *)val.value;
    OK(buf[0] == 0x10, "value 0x%x", buf[0]);
    free(val.value); val.value = NULL;
    OK(read_constant(&val, PROTO_ID_ETH, PFTYPE_NUMBER, "no") == true, "should be accepted");
    OK_FATAL(val.value != NULL, "have buffer");
    buf = (unsigned char *)val.value;
    OK(buf[0] == 0x00, "value 0x%x", buf[0]);
    free(val.value); val.value = NULL;
    OK(read_constant(&val, PROTO_ID_ETH, PFTYPE_NUMBER, "yes") == true, "should be accepted");
    OK_FATAL(val.value != NULL, "have buffer");
    buf = (unsigned char *)val.value;
    OK(buf[0] == 0x10, "value 0x%x", buf[0]);
    free(val.value); val.value = NULL;
    OK(read_constant(&val, PROTO_ID_ETH, PFTYPE_NUMBER, "on") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_ETH, PFTYPE_NUMBER, "off") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_ETH, PFTYPE_NUMBER, "árvíztűrő") == false, "should be rejected");

    val.bitcount = 10;
    OK(read_constant(&val, PROTO_ID_ETH, PFTYPE_NUMBER, "yes") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_ETH, PFTYPE_NUMBER, "árvíztűrő") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_ETH, PFTYPE_NUMBER, "2000") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_ETH, PFTYPE_NUMBER, "-20") == false, "should be rejected");

    OK(read_constant(&val, PROTO_ID_ETH, PFTYPE_NUMBER, "1000") == true, "should be accepted");
    OK_FATAL(val.value != NULL, "have buffer");
    buf = (unsigned char *)val.value;
    // 1000 = 0x3e8 = 0011 1110 1000
    // offset=3 bitcount=10 '000x xxxx xxxx x000'
    //                      '0001 1111 0100 0000' = 0x1f 0x40
    OK(buf[0] == 0x1f, "value 0x%x", buf[0]);
    OK(buf[1] == 0x40, "value 0x%x", buf[1]);
    free(val.value); val.value = NULL;

    OK(read_constant(&val, PROTO_ID_ETH, PFTYPE_NUMBER, "0x1f4") == true, "should be accepted");
    OK_FATAL(val.value != NULL, "have buffer");
    buf = (unsigned char *)val.value;
    // 500 = 0x1f4 = 0001 1111 0100
    // offset=3 bitcount=10 '000x xxxx xxxx x000'
    //                      '0000 1111 1010 0000' = 0x0f 0xa0
    OK(buf[0] == 0x0f, "value 0x%x", buf[0]);
    OK(buf[1] == 0xa0, "value 0x%x", buf[1]);
    free(val.value); val.value = NULL;

    OK(read_constant(&val, PROTO_ID_ETH, PFTYPE_NUMBER, "0764") == true, "should be accepted");
    OK_FATAL(val.value != NULL, "have buffer");
    buf = (unsigned char *)val.value;
    // 500 = 0x1f4 = 0001 1111 0100 = octal 0764
    // offset=3 bitcount=10 '000x xxxx xxxx x000'
    //                      '0000 1111 1010 0000' = 0x0f 0xa0
    OK(buf[0] == 0x0f, "value 0x%x", buf[0]);
    OK(buf[1] == 0xa0, "value 0x%x", buf[1]);
    free(val.value); val.value = NULL;


    // PFTYPE_TTL should accept normal numbers
    // doesn't verify that the given protocol has ttl
    OK(read_constant(&val, PROTO_ID_ETH, PFTYPE_TTL, "yes") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_ETH, PFTYPE_TTL, "0x1f4") == true, "should be accepted");
    OK_FATAL(val.value != NULL, "have buffer");
    buf = (unsigned char *)val.value;
    // 500 = 0x1f4 = 0001 1111 0100
    // offset=3 bitcount=10 '000x xxxx xxxx x000'
    //                      '0000 1111 1010 0000' = 0x0f 0xa0
    OK(buf[0] == 0x0f, "value 0x%x", buf[0]);
    OK(buf[1] == 0xa0, "value 0x%x", buf[1]);
    free(val.value); val.value = NULL;


    // PFTYPE_CHECKSUM should accept normal numbers
    // doesn't verify that the given protocol has checksum
    OK(read_constant(&val, PROTO_ID_ETH, PFTYPE_CHECKSUM, "yes") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_ETH, PFTYPE_CHECKSUM, "0x1f4") == true, "should be accepted");
    OK_FATAL(val.value != NULL, "have buffer");
    buf = (unsigned char *)val.value;
    // 500 = 0x1f4 = 0001 1111 0100
    // offset=3 bitcount=10 '000x xxxx xxxx x000'
    //                      '0000 1111 1010 0000' = 0x0f 0xa0
    OK(buf[0] == 0x0f, "value 0x%x", buf[0]);
    OK(buf[1] == 0xa0, "value 0x%x", buf[1]);
    free(val.value); val.value = NULL;


    // PFTYPE_TSNSEQ should accept normal numbers (with warning)
    OK(read_constant(&val, PROTO_ID_ETH, PFTYPE_TSNSEQ, "yes") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_ETH, PFTYPE_TSNSEQ, "0x1f4") == true, "should be accepted");
    OK_FATAL(val.value != NULL, "have buffer");
    buf = (unsigned char *)val.value;
    // 500 = 0x1f4 = 0001 1111 0100
    // offset=3 bitcount=10 '000x xxxx xxxx x000'
    //                      '0000 1111 1010 0000' = 0x0f 0xa0
    OK(buf[0] == 0x0f, "value 0x%x", buf[0]);
    OK(buf[1] == 0xa0, "value 0x%x", buf[1]);
    free(val.value); val.value = NULL;


    // PFTYPE_TSNTSTAMP should accept normal numbers (with warning)
    OK(read_constant(&val, PROTO_ID_ETH, PFTYPE_TSNTSTAMP, "yes") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_ETH, PFTYPE_TSNTSTAMP, "0x1f4") == true, "should be accepted");
    OK_FATAL(val.value != NULL, "have buffer");
    buf = (unsigned char *)val.value;
    // 500 = 0x1f4 = 0001 1111 0100
    // offset=3 bitcount=10 '000x xxxx xxxx x000'
    //                      '0000 1111 1010 0000' = 0x0f 0xa0
    OK(buf[0] == 0x0f, "value 0x%x", buf[0]);
    OK(buf[1] == 0xa0, "value 0x%x", buf[1]);
    free(val.value); val.value = NULL;


    // PFTYPE_NEXTHEADER should accept normal numbers, protocol names
    // no validation should happen on numbers, the user knows what they're doing
    // in this case ARP has no nexthdr field, and 500 is an invalid value anyway
    OK(read_constant(&val, PROTO_ID_ARP, PFTYPE_NEXTHEADER, "0x1f4") == true, "should be accepted");
    OK_FATAL(val.value != NULL, "have buffer");
    buf = (unsigned char *)val.value;
    // 500 = 0x1f4 = 0001 1111 0100
    // offset=3 bitcount=10 '000x xxxx xxxx x000'
    //                      '0000 1111 1010 0000' = 0x0f 0xa0
    OK(buf[0] == 0x0f, "value 0x%x", buf[0]);
    OK(buf[1] == 0xa0, "value 0x%x", buf[1]);
    free(val.value); val.value = NULL;

    // decoding uses the protocol info, so the string is validated
    OK(read_constant(&val, PROTO_ID_ARP, PFTYPE_NEXTHEADER, "cvlan") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_NEXTHEADER, "dcw") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_ETH, PFTYPE_NEXTHEADER, "udp") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_IPv4, PFTYPE_NEXTHEADER, "rtag") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_IPv6, PFTYPE_NEXTHEADER, "svlan") == false, "should be rejected");

    val.bitoffset = 8;
    val.bitcount = 16;
    OK(read_constant(&val, PROTO_ID_RTAG, PFTYPE_NEXTHEADER, "svlan") == true, "should be accepted");
    OK_FATAL(val.value != NULL, "have buffer");
    buf = (unsigned char *)val.value;
    OK(buf[0] == 0x88, "value 0x%x", buf[0]);
    OK(buf[1] == 0xa8, "value 0x%x", buf[1]);
    free(val.value); val.value = NULL;

    OK(read_constant(&val, PROTO_ID_IPv4, PFTYPE_NEXTHEADER, "tcp") == true, "should be accepted");
    OK_FATAL(val.value != NULL, "have buffer");
    buf = (unsigned char *)val.value;
    OK(buf[0] == 0x00, "value 0x%x", buf[0]);
    OK(buf[1] == 0x06, "value 0x%x", buf[1]);
    free(val.value); val.value = NULL;

    OK(read_constant(&val, PROTO_ID_IPv6, PFTYPE_NEXTHEADER, "udp") == true, "should be accepted");
    OK_FATAL(val.value != NULL, "have buffer");
    buf = (unsigned char *)val.value;
    OK(buf[0] == 0x00, "value 0x%x", buf[0]);
    OK(buf[1] == 0x11, "value 0x%x", buf[1]);
    free(val.value); val.value = NULL;


    // PFTYPE_IPV4ADDRESS
    val.bitoffset = 8;
    val.bitcount = 32;
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_IPV4ADDRESS, "") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_IPV4ADDRESS, "árvíz") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_IPV4ADDRESS, "1") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_IPV4ADDRESS, "1.2") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_IPV4ADDRESS, "1.2.3") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_IPV4ADDRESS, "1.2.3.4") == true, "should be accepted");
    OK_FATAL(val.value != NULL, "have buffer");
    buf = (unsigned char *)val.value;
    OK(memcmp(buf, "\x01\x02\x03\x04", 4) == 0, "value %u.%u.%u.%u", buf[0], buf[1], buf[2], buf[3]);
    free(val.value); val.value = NULL;
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_IPV4ADDRESS, "1.2.3.4.5") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_IPV4ADDRESS, "1.2.3.4:5") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_IPV4ADDRESS, "1.2.3.4test") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_IPV4ADDRESS, "1.2.3.4 test") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_IPV4ADDRESS, "255.255.256.255") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_IPV4ADDRESS, "1.2.3.4/23") == true, "should be accepted");
    OK_FATAL(val.value != NULL, "have buffer");
    buf = (unsigned char *)val.value;
    OK(memcmp(buf, "\x01\x02\x02\x00", 4) == 0, "value %u.%u.%u.%u", buf[0], buf[1], buf[2], buf[3]);
    OK(val.bitcount == 23, "prefix %u", val.bitcount);
    free(val.value); val.value = NULL;
    val.bitcount = 32;
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_IPV4ADDRESS, "1.2.3.4/") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_IPV4ADDRESS, "1.2.3.4/many") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_IPV4ADDRESS, "1.2.3.4/5many") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_IPV4ADDRESS, "1.2.3.4/33") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_IPV4ADDRESS, "1.2.3.4/3/more") == false, "should be rejected");


    // PFTYPE_IPV6ADDRESS
    val.bitoffset = 8;
    val.bitcount = 128;
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_IPV6ADDRESS, "") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_IPV6ADDRESS, "árvíz") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_IPV6ADDRESS, "a") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_IPV6ADDRESS, "a:b") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_IPV6ADDRESS, "a::b") == true, "should be accepted");
    OK_FATAL(val.value != NULL, "have buffer");
    buf = (unsigned char *)val.value;
    OK(memcmp(buf, "\x00\x0a\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x0b", 16) == 0,
            "value %x %x:%x %x:%x %x:%x %x:%x %x:%x %x:%x %x:%x %x",
            buf[ 0], buf[ 1], buf[ 2], buf[ 3], buf[ 4], buf[ 5], buf[ 6], buf[ 7],
            buf[ 8], buf[ 9], buf[10], buf[11], buf[12], buf[13], buf[14], buf[15]);
    free(val.value); val.value = NULL;
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_IPV6ADDRESS, "a:::b") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_IPV6ADDRESS, "a::b::c") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_IPV6ADDRESS, "[a::b]:64") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_IPV6ADDRESS, "a::btest") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_IPV6ADDRESS, "a::b test") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_IPV6ADDRESS, "0123:4567:89ab:cdef:0123:4567:89ab:cdef") == true, "should be accepted");
    OK_FATAL(val.value != NULL, "have buffer");
    buf = (unsigned char *)val.value;
    OK(memcmp(buf, "\x01\x23\x45\x67\x89\xab\xcd\xef\x01\x23\x45\x67\x89\xab\xcd\xef", 16) == 0,
            "value %x %x:%x %x:%x %x:%x %x:%x %x:%x %x:%x %x:%x %x",
            buf[ 0], buf[ 1], buf[ 2], buf[ 3], buf[ 4], buf[ 5], buf[ 6], buf[ 7],
            buf[ 8], buf[ 9], buf[10], buf[11], buf[12], buf[13], buf[14], buf[15]);
    free(val.value); val.value = NULL;
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_IPV6ADDRESS, "a::b/53") == true, "should be accepted");
    OK_FATAL(val.value != NULL, "have buffer");
    buf = (unsigned char *)val.value;
    OK(memcmp(buf, "\x00\x0a\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 16) == 0,
            "value %x %x:%x %x:%x %x:%x %x:%x %x:%x %x:%x %x:%x %x",
            buf[ 0], buf[ 1], buf[ 2], buf[ 3], buf[ 4], buf[ 5], buf[ 6], buf[ 7],
            buf[ 8], buf[ 9], buf[10], buf[11], buf[12], buf[13], buf[14], buf[15]);
    OK(val.bitcount == 53, "prefix %u", val.bitcount);
    free(val.value); val.value = NULL;
    val.bitcount = 128;
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_IPV6ADDRESS, "a::b/") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_IPV6ADDRESS, "a::b/12many") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_IPV6ADDRESS, "a::b/many") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_IPV6ADDRESS, "a::b/129") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_IPV6ADDRESS, "a::b/12/more") == false, "should be rejected");


    // PFTYPE_MACADDRESS
    val.bitoffset = 8;
    val.bitcount = 48;
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_MACADDRESS, "") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_MACADDRESS, "árvíz") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_MACADDRESS, "1") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_MACADDRESS, "1:") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_MACADDRESS, "1::") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_MACADDRESS, "1:2:3:4:5:6") == true, "should be accepted");
    OK_FATAL(val.value != NULL, "have buffer");
    buf = (unsigned char *)val.value;
    OK(memcmp(buf, "\x01\x02\x03\x04\x05\x06", 6) == 0, "value %x:%x:%x:%x:%x:%x",
            buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);
    free(val.value); val.value = NULL;
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_MACADDRESS, "a:b:c:d:e:f") == true, "should be accepted");
    OK_FATAL(val.value != NULL, "have buffer");
    buf = (unsigned char *)val.value;
    OK(memcmp(buf, "\x0a\x0b\x0c\x0d\x0e\x0f", 6) == 0, "value %x:%x:%u:%x:%x:%x",
            buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);
    free(val.value); val.value = NULL;
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_MACADDRESS, "1:2:3:4:5:6:7") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_MACADDRESS, "1:2:3:4:5:6test") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_MACADDRESS, "1:2:3:4:5:6and something") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_MACADDRESS, "1:2:3:4:5:6 test") == false, "should be rejected");
    // ether_pton() only accepts the colon notation
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_MACADDRESS, "1-2-3-4-5-6") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_MACADDRESS, "1_2_3_4_5_6") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_MACADDRESS, "a:b:c:d:e:f/13") == true, "should be accepted");
    OK_FATAL(val.value != NULL, "have buffer");
    buf = (unsigned char *)val.value;
    OK(memcmp(buf, "\x0a\x08\x00\x00\x00\x00", 6) == 0, "value %x:%x:%x:%x:%x:%x",
            buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);
    OK(val.bitcount == 13, "prefix %u", val.bitcount);
    free(val.value); val.value = NULL;
    val.bitcount = 48;
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_MACADDRESS, "1:2:3:4:5:6/") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_MACADDRESS, "1:2:3:4:5:6/many") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_MACADDRESS, "1:2:3:4:5:6/7many") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_MACADDRESS, "1:2:3:4:5:6/49") == false, "should be rejected");
    OK(read_constant(&val, PROTO_ID_MPLS, PFTYPE_MACADDRESS, "1:2:3:4:5:6/7/more") == false, "should be rejected");
}

TEST_CASES = {
    {"foreach stages", test_foreach_stages},
    {"foreach tokens", test_foreach_tokens},
    {"parse assignment", test_parse_assignment},
    {"parse fieldname", test_parse_fieldname},
    {"header type from name", test_header_type_from_name},
    {"read boolean", test_read_boolean},
    {"prepare constant number", test_prepare_constant_number},
    {"read constant", test_read_constant},
    {NULL, NULL}
};

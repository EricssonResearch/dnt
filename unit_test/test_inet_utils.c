
#include "testing.h"

#include "inet_utils.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>
#include <endian.h>

TEST_INIT("Inet Utils");

static void test_ip_port(void)
{
    char *ip_str = NULL;
    unsigned port;
    bool res;

#define UNDEF 0xffffffff

#define TRY_VALID(_input, _ip, _port)                   \
    free(ip_str);                                       \
    ip_str = NULL;                                      \
    port = UNDEF;                                       \
    res = parse_ip_port(_input, &ip_str, &port);        \
    OK(res == true, "valid");                           \
    OK_FATAL(ip_str != NULL, "have ip string");         \
    OK(strcmp(ip_str, _ip) == 0, "ip is %s", ip_str);   \
    OK(port == _port, "port %u 0x%x", port, port)

#define TRY_INVALID(_input)                             \
    free(ip_str);                                       \
    ip_str = NULL;                                      \
    port = UNDEF;                                       \
    res = parse_ip_port(_input, &ip_str, &port);        \
    OK(res == false, "invalid");                        \
    OK_FATAL(ip_str == NULL, "no ip string");           \
    OK(port == UNDEF, "port unset")

    // IPv4

    TRY_VALID("192.168.1.2", "192.168.1.2", UNDEF);
    TRY_VALID("255.255.255.255", "255.255.255.255", UNDEF);
    TRY_VALID("0.0.0.0", "0.0.0.0", UNDEF);
    TRY_INVALID("00.00.00.00");
    TRY_INVALID("255.256.255.255");
    TRY_INVALID("1.2.3");
    TRY_INVALID("1.2.3.4.5");
    TRY_INVALID("1.2.3.4.text");
    TRY_INVALID("1.2.3.4text");
    TRY_INVALID("[1.2.3.4]");
    TRY_INVALID("[1.2.3.4");
    TRY_INVALID("1.2.3.4]");

    TRY_VALID("1.2.3.4:5", "1.2.3.4", 5);
    TRY_VALID("1.2.3.4:0", "1.2.3.4", 0);
    TRY_VALID("1.2.3.4:65535", "1.2.3.4", 65535);
    TRY_INVALID("1.2.3.4:65536");
    TRY_INVALID("1.2.3.4:");
    TRY_INVALID(":1");
    TRY_INVALID("1.2.3.4:text");
    TRY_INVALID("1.2.3.4:5text");
    TRY_INVALID("[1.2.3.4]:5");
    TRY_INVALID("[1.2.3.4:5");
    TRY_INVALID("1.2.3.4]:5");

    // IPv6

    TRY_VALID("::", "::", UNDEF);
    TRY_VALID("0::", "0::", UNDEF);
    TRY_VALID("1:2:3:4:5:6:7:8", "1:2:3:4:5:6:7:8", UNDEF);
    TRY_VALID("1:2:3::7:8", "1:2:3::7:8", UNDEF);
    /* this one triggers a bug in valgrind:
     *   inet_pton uses memmove with overlapping regions (valid)
     *   with FORTIFY_SOURCE it turns into __memmove_chk@GLIBC
     *   but valgrind replaces it with its __memcpy_chk
     *   and that one doesn't allow overlapping regions
     *   (according to bugreports this memcpy-memmove confusion
     *    seems to have been fixed a few times in valgrind...)
     */
    //TRY_VALID("a:b:c::d:e:f", "a:b:c::d:e:f", UNDEF);
    TRY_VALID("[b::d]", "b::d", UNDEF);
    TRY_INVALID("1:2:3:4:5:6:7:8:9");
    TRY_INVALID("1::3:4:5:6::8:9");
    TRY_INVALID("a:b:c::d:e:f:g");
    TRY_INVALID("a:b:c::d:e:fg");
    TRY_INVALID("[b::d");
    TRY_INVALID("b::d]");

    TRY_VALID("[::]:0", "::", 0);
    TRY_VALID("[1:2:3:4:5:6:7:8]:9", "1:2:3:4:5:6:7:8", 9);
    TRY_VALID("[::]:65535", "::", 65535);
    TRY_INVALID("[::]:65536");
    TRY_INVALID("::/9");
    TRY_INVALID("[b::d]:");
    TRY_INVALID("b::d]:");
    TRY_INVALID("[b::d]:text");
    TRY_INVALID("[b::d]:5text");
    TRY_INVALID("[b::d]10");

#undef TRY_VALID
#undef TRY_INVALID
#undef UNDEF
}

static void test_ether_pton(void)
{
    unsigned char buf[6];
    OK(ether_pton("", buf) == 0, "should be invalid");
    OK(ether_pton("1", buf) == 0, "should be invalid");
    OK(ether_pton("1:", buf) == 0, "should be invalid");
    OK(ether_pton("1:2", buf) == 0, "should be invalid");
    OK(ether_pton("1:2:", buf) == 0, "should be invalid");
    OK(ether_pton("1:2:3", buf) == 0, "should be invalid");
    OK(ether_pton("1:2:3:", buf) == 0, "should be invalid");
    OK(ether_pton("1:2:3:4", buf) == 0, "should be invalid");
    OK(ether_pton("1:2:3:4:", buf) == 0, "should be invalid");
    OK(ether_pton("1:2:3:4:5", buf) == 0, "should be invalid");
    OK(ether_pton("1:2:3:4:5:", buf) == 0, "should be invalid");
    OK(ether_pton(":::::", buf) == 0, "should be invalid");
    OK(ether_pton("1:2:3:4:5:6", buf) == 1, "should be valid");
    OK(memcmp(buf, "\x01\x02\x03\x04\x05\x06", 6) == 0, "result wrong %x:%x:%x:%x:%x:%x",
            buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);
    OK(ether_pton("01:02:03:04:05:06", buf) == 1, "should be valid");
    OK(memcmp(buf, "\x01\x02\x03\x04\x05\x06", 6) == 0, "result wrong %x:%x:%x:%x:%x:%x",
            buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);
    OK(ether_pton("001:02:03:04:05:06", buf) == 0, "should be invalid");
    OK(ether_pton("01:002:03:04:05:06", buf) == 0, "should be invalid");
    OK(ether_pton("01:02:003:04:05:06", buf) == 0, "should be invalid");
    OK(ether_pton("01:02:03:004:05:06", buf) == 0, "should be invalid");
    OK(ether_pton("01:02:03:04:005:06", buf) == 0, "should be invalid");
    OK(ether_pton("01:02:03:04:05:006", buf) == 0, "should be invalid");
    OK(ether_pton("aa:bb:cc:dd:ee:ff", buf) == 1, "should be valid");
    OK(memcmp(buf, "\xaa\xbb\xcc\xdd\xee\xff", 6) == 0, "result wrong %x:%x:%x:%x:%x:%x",
            buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);
    OK(ether_pton("1:2:3:4:5:6:", buf) == 0, "should be invalid");
    OK(ether_pton("1:2:3:4:5:6:7", buf) == 0, "should be invalid");
    OK(ether_pton("1:2:3:4:5:6x", buf) == 0, "should be invalid");
    OK(ether_pton("1:2:3:4:5:66x", buf) == 0, "should be invalid");
    OK(ether_pton("1:2:3:4:5:6 x", buf) == 0, "should be invalid");
    OK(ether_pton("1:2:3:4:5:66 x", buf) == 0, "should be invalid");
    OK(ether_pton("1:2:3:4:5:6/", buf) == 0, "should be invalid");
    OK(ether_pton("1:2:3:4:5:6/7", buf) == 0, "should be invalid");
    OK(ether_pton("1-2-3-4-5-6", buf) == 0, "should be invalid");
    OK(ether_pton("1_2_3_4_5_6", buf) == 0, "should be invalid");
}

static void test_ether_ntop(void)
{
    unsigned char eth1[] = {1, 2, 3, 4, 5, 6};
    unsigned char eth2[] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    char eth_str[ETHER_ADDSTRLEN];
    OK(ether_ntop(eth1, eth_str, sizeof(eth_str)) == eth_str, "should be valid");
    OK(strcmp(eth_str, "01:02:03:04:05:06") == 0, "address '%s'", eth_str);
    OK(ether_ntop(eth2, eth_str, sizeof(eth_str)) == eth_str, "should be valid");
    OK(strcmp(eth_str, "aa:bb:cc:dd:ee:ff") == 0, "address '%s'", eth_str);

    OK(ether_ntop(eth1, eth_str, sizeof(eth_str)-1) == NULL, "dst buffer should be too small");
}

static void test_mac_vlan(void)
{
    char *mac;
    unsigned vlan;
    OK(parse_mac_vlan("", &mac, &vlan) == false, "should be invalid");
    OK(parse_mac_vlan("1", &mac, &vlan) == false, "should be invalid");
    OK(parse_mac_vlan("1:", &mac, &vlan) == false, "should be invalid");
    OK(parse_mac_vlan("1:2", &mac, &vlan) == false, "should be invalid");
    OK(parse_mac_vlan("1:2:", &mac, &vlan) == false, "should be invalid");
    OK(parse_mac_vlan("1:2:3", &mac, &vlan) == false, "should be invalid");
    OK(parse_mac_vlan("1:2:3:", &mac, &vlan) == false, "should be invalid");
    OK(parse_mac_vlan("1:2:3:4", &mac, &vlan) == false, "should be invalid");
    OK(parse_mac_vlan("1:2:3:4:", &mac, &vlan) == false, "should be invalid");
    OK(parse_mac_vlan("1:2:3:4:5", &mac, &vlan) == false, "should be invalid");
    OK(parse_mac_vlan("1:2:3:4:5:", &mac, &vlan) == false, "should be invalid");
    OK(parse_mac_vlan(":::::", &mac, &vlan) == false, "should be invalid");
    OK(parse_mac_vlan("1:2:3:4:5:6", &mac, &vlan) == true, "should be valid");
    OK(strcmp(mac, "1:2:3:4:5:6") == 0, "mac wrong '%s'", mac);
    OK(vlan == 0, "vlan wrong %u", vlan);
    free(mac);
    OK(parse_mac_vlan("1:2:3:4:5:6+", &mac, &vlan) == false, "should be invalid");
    OK(parse_mac_vlan("1:2:3:4:5:6+vlan", &mac, &vlan) == false, "should be invalid");
    OK(parse_mac_vlan("+", &mac, &vlan) == false, "should be invalid");
    OK(parse_mac_vlan("+vlan", &mac, &vlan) == false, "should be invalid");
    OK(parse_mac_vlan("1:2:3:4:5:6+0", &mac, &vlan) == true, "should be valid");
    OK(strcmp(mac, "1:2:3:4:5:6") == 0, "mac wrong '%s'", mac);
    OK(vlan == 0, "vlan wrong %u", vlan);
    free(mac);
    OK(parse_mac_vlan("1:2:3:4:5:6+100", &mac, &vlan) == true, "should be valid");
    OK(strcmp(mac, "1:2:3:4:5:6") == 0, "mac wrong '%s'", mac);
    OK(vlan == 100, "vlan wrong %u", vlan);
    free(mac);
    OK(parse_mac_vlan("1:2:3:4:5:6+0x1234", &mac, &vlan) == true, "should be valid");
    OK(strcmp(mac, "1:2:3:4:5:6") == 0, "mac wrong '%s'", mac);
    OK(vlan == 0x1234, "vlan wrong %u", vlan);
    free(mac);
    OK(parse_mac_vlan("1:2:3:4:5:6+0xffff", &mac, &vlan) == true, "should be valid");
    OK(strcmp(mac, "1:2:3:4:5:6") == 0, "mac wrong '%s'", mac);
    OK(vlan == 0xffff, "vlan wrong %u", vlan);
    free(mac);
    OK(parse_mac_vlan("1:2:3:4:5:6+0100", &mac, &vlan) == true, "should be valid");
    OK(strcmp(mac, "1:2:3:4:5:6") == 0, "mac wrong '%s'", mac);
    OK(vlan == 64, "vlan wrong %u", vlan);
    free(mac);
}

// how to run this on big-endian without access to such hardware:
// (MIPS is BE and doesn't like unaligned memory access)
//  apt install gcc-mips-linux-gnu g++-mips-linux-gnu qemu-user
//  mips-linux-gnu-gcc -static -I.. -DTESTING test_inet_utils.c ../inet_utils.c -o test_inet_utils_mips
//  qemu-mips ./test_inet_utils_mips

static void test_checksum(void)
{
    struct {
        const char *buf;
        unsigned len;
        uint32_t sum32_le;
        uint32_t sum32_be;
        uint16_t sum16; // after the 1's complement, in host order
    } data[] = {
        // test short buffer (corner case, and also easy to compute by hand)
        { "a",    1, 0x0061, 0x6100, 0x9eff }, // [a 0]
        { "ab",   2, 0x6261, 0x6162, 0x9e9d },
        { "abc",  3, 0x62c4, 0xc462, 0x3b9d }, // [a b] [c 0]
        { "abcd", 4, 0xc6c4, 0xc4c6, 0x3b39 },

        // numerical example from RFC 1071
        { "\x00\x01\xf2\x03\xf4\xf5\xf6\xf7", 8, 0x1f2dc, 0x2ddf0, 0x220d },
        // the order of the words should not matter
        { "\xf2\x03\x00\x01\xf4\xf5\xf6\xf7", 8, 0x1f2dc, 0x2ddf0, 0x220d },
        { "\xf4\xf5\xf6\xf7\xf2\x03\x00\x01", 8, 0x1f2dc, 0x2ddf0, 0x220d },

        { "very long input string to test overflowing the accumulator with"
            " more data than it can handle and then maybe we'll see a new error"
            " that we haven't encountered in the previous test cases?", 185, 0x00211308, 0x002328f0, 0xd6ec },
    };

    //printf(" strlen %zu ", strlen(data[7].buf));

// sum32 is not reliable if we do the 32bit optimization
//#define TEST_SUM32

    if (__BYTE_ORDER == __LITTLE_ENDIAN)
        printf("running on little-endian");
    else if (__BYTE_ORDER == __BIG_ENDIAN)
        printf("running on big-endian");
    else
        printf("running on unknown endian");

    OK(csum_partial(0, 0, 0) == 0, "null buffer shouldn't crash");
    OK(csum_partial((const uint8_t*)1, 0, 0) == 0, "unaligned buffer shouldn't crash");
    OK(csum_partial((const uint8_t*)42, 0, 123450) == 123450, "sum should be unchanged");

    for (unsigned i=0; i<ARRAY_SIZE(data); i++) {
        // malloc returns pointers aligned at 8 byte
        uint8_t *buf = (uint8_t*)malloc((data[i].len+1)*sizeof(uint8_t));
        memcpy(buf, data[i].buf, data[i].len);
        uint32_t sum32 = csum_partial(buf, data[i].len, 0);
        uint16_t sum16 = csum_fold(sum32);
#ifdef TEST_SUM32
        if (__BYTE_ORDER == __LITTLE_ENDIAN)
            OK(sum32 == data[i].sum32_le, "%u sum32LE 0x%.08x vs 0x%.08x", i, sum32, data[i].sum32_le);
        else if (__BYTE_ORDER == __BIG_ENDIAN)
            OK(sum32 == data[i].sum32_be, "%u sum32BE 0x%.08x vs 0x%.08x", i, sum32, data[i].sum32_be);
#endif
        OK(sum16 == data[i].sum16, "%u sum16 0x%.04x vs 0x%.04x", i, sum16, data[i].sum16);

        // now our data starts at odd offset, but we should get the same result
        memcpy(buf+1, data[i].buf, data[i].len);
        sum32 = csum_partial(buf+1, data[i].len, 0);
        sum16 = csum_fold(sum32);
#ifdef TEST_SUM32
        if (__BYTE_ORDER == __LITTLE_ENDIAN)
            OK(sum32 == data[i].sum32_le, "%u sum32LE 0x%.08x vs 0x%.08x", i, sum32, data[i].sum32_le);
        else if (__BYTE_ORDER == __BIG_ENDIAN)
            OK(sum32 == data[i].sum32_be, "%u sum32BE 0x%.08x vs 0x%.08x", i, sum32, data[i].sum32_be);
#endif
        OK(sum16 == data[i].sum16, "%u sum16 0x%.04x vs 0x%.04x", i, sum16, data[i].sum16);

        // compute sum in two parts, total sum must be the same
        // (we can only split at even positions)
        memcpy(buf, data[i].buf, data[i].len);
        for (unsigned j=0; j<data[i].len; j+=2) {
            sum32 = csum_partial(buf, j, 0);
            sum32 = csum_partial(buf+j, data[i].len-j, sum32);
            sum16 = csum_fold(sum32);
#ifdef TEST_SUM32
            if (__BYTE_ORDER == __LITTLE_ENDIAN)
                OK(sum32 == data[i].sum32_le, "%u %u sum32LE 0x%.08x vs 0x%.08x", i, j, sum32, data[i].sum32_le);
            else if (__BYTE_ORDER == __BIG_ENDIAN)
                OK(sum32 == data[i].sum32_be, "%u %u sum32BE 0x%.08x vs 0x%.08x", i, j, sum32, data[i].sum32_be);
#endif
            OK(sum16 == data[i].sum16, "%u %u sum16 0x%.04x vs 0x%.04x", i, j, sum16, data[i].sum16);
        }
        free(buf);
    }
}

TEST_CASES = {
    {"parse_ip_port", test_ip_port},
    {"ether_pton", test_ether_pton},
    {"ether_ntop", test_ether_ntop},
    {"parse_mac_vlan", test_mac_vlan},
    {"checksum", test_checksum},
    {NULL, NULL}
};

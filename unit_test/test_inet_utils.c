
#include "testing.h"

#include "inet_utils.h"

#include <stdlib.h>
#include <string.h>

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

TEST_CASES = {
    {"parse_ip_port", test_ip_port},
    {"ether_pton", test_ether_pton},
    {"ether_ntop", test_ether_ntop},
    {"parse_mac_vlan", test_mac_vlan},
    {NULL, NULL}
};

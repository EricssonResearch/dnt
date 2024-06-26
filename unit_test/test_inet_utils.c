
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

TEST_CASES = {
    {"IP Port", test_ip_port},
    {NULL, NULL}
};

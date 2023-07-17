
#include "testing.h"

#include "json.h"
#include "utils.h"

#include <string.h>
#include <stdlib.h>

TEST_INIT("Json Write");

static void test_write(void)
{
#define CHECK                                                               \
    unsigned slen = strlen(expected);                                       \
    unsigned glen;                                                          \
    char *got = json_serialize(val, &glen);                                 \
    json_delete(val);                                                       \
    OK(glen == slen, "length %u != %u", glen, slen);                        \
    OK(strcmp(expected, got) == 0, "mismatched result '%s'", got);          \
    free(got)

    do {
        const char *expected = "null";
        struct JsonValue *val = json_null();
        CHECK;
    } while (0);

    do {
        const char *expected = "true";
        struct JsonValue *val = json_true();
        CHECK;
    } while (0);

    do {
        const char *expected = "false";
        struct JsonValue *val = json_false();
        CHECK;
    } while (0);

    do {
        const char *expected = "53";
        struct JsonValue *val = json_number(53);
        CHECK;
    } while (0);

    do {
        const char *expected = "-153";
        struct JsonValue *val = json_number(-153);
        CHECK;
    } while (0);

    do {
        const char *expected = "3.210000";
        struct JsonValue *val = json_number(3.21);
        CHECK;
    } while (0);

    do {
        const char *expected = "-9.900000";
        struct JsonValue *val = json_number(-9.9);
        CHECK;
    } while (0);

    do {
        const char *expected = "\"short string\"";
        struct JsonValue *val = json_string("short string");
        CHECK;
    } while (0);

    do {
        const char *expected = "\"JSON (JavaScript Object Notation) is a lightweight data-interchange format. It is easy for humans to read and write. It is easy for machines to parse and generate. It is based on a subset of the JavaScript Programming Language Standard ECMA-262 3rd Edition - December 1999. JSON is a text format that is completely language independent but uses conventions that are familiar to programmers of the C-family of languages, including C, C++, C#, Java, JavaScript, Perl, Python, and many others. These properties make JSON an ideal data-interchange language.\"";
        struct JsonValue *val = json_string("JSON (JavaScript Object Notation) is a lightweight data-interchange format. It is easy for humans to read and write. It is easy for machines to parse and generate. It is based on a subset of the JavaScript Programming Language Standard ECMA-262 3rd Edition - December 1999. JSON is a text format that is completely language independent but uses conventions that are familiar to programmers of the C-family of languages, including C, C++, C#, Java, JavaScript, Perl, Python, and many others. These properties make JSON an ideal data-interchange language.");
        CHECK;
    } while (0);

    do {
        const char *expected = "[]";
        struct JsonValue *val = json_array();
        CHECK;
    } while (0);

    do {
        const char *expected = "[\"some string\"]";
        struct JsonValue *val = json_array();
        json_array_unshift(val, json_string("some string"));
        CHECK;
    } while (0);

    do {
        const char *expected = "[-13,true,\"some string\"]";
        struct JsonValue *val = json_array();
        json_array_unshift(val, json_string("some string"));
        json_array_unshift(val, json_true());
        json_array_unshift(val, json_number(-13));
        CHECK;
    } while (0);

    do {
        const char *expected = "{}";
        struct JsonValue *val = json_object();
        CHECK;
    } while (0);

    do {
        const char *expected = "{\"key\":13}";
        struct JsonValue *val = json_object();
        json_object_insert(val, "key", json_number(13));
        CHECK;
    } while (0);

    do {
        const char *expected = "{\"key\":\"value\",\"key2\":false,\"something\":-99}";
        struct JsonValue *val = json_object();
        json_object_insert(val, "key2", json_false());
        json_object_insert(val, "something", json_number(-99));
        json_object_insert(val, "key", json_string("value"));
        CHECK;
    } while (0);

    //TODO also add items to the inner array/object

    do {
        const char *expected = "[[],\"some string\"]";
        struct JsonValue *val = json_array();
        json_array_unshift(val, json_string("some string"));
        json_array_unshift(val, json_array());
        CHECK;
    } while (0);

    do {
        const char *expected = "[{},\"some string\"]";
        struct JsonValue *val = json_array();
        json_array_unshift(val, json_string("some string"));
        json_array_unshift(val, json_object());
        CHECK;
    } while (0);

    do {
        const char *expected = "{\"a\":[],\"key\":13}";
        struct JsonValue *val = json_object();
        json_object_insert(val, "key", json_number(13));
        json_object_insert(val, "a", json_array());
        CHECK;
    } while (0);

    do {
        const char *expected = "{\"a\":{},\"key\":13}";
        struct JsonValue *val = json_object();
        json_object_insert(val, "key", json_number(13));
        json_object_insert(val, "a", json_object());
        CHECK;
    } while (0);
}

static void test_realloc(void)
{
    //TODO generate input such that CHECK_BUF() will realloc the buffer
    //      we can know that BUFFER_INCREMENT is 128
    //      we should verify all instances of CHECK_BUF()
    SKIP("this needs more planning");
}

TEST_CASES = {
    {"Write", test_write},
    {"Realloc", test_realloc},
    {NULL, NULL}
};

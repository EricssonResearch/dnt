
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
    OK_FATAL(got != NULL, "got serialized value");                          \
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

    do {
        const char *expected = "[[],[\"inner string\"],\"some string\"]";
        struct JsonValue *val = json_array();
        json_array_unshift(val, json_string("some string"));
        struct JsonValue *inner_arr = json_array();
        json_array_unshift(inner_arr, json_string("inner string"));
        json_array_unshift(val, inner_arr);
        json_array_unshift(val, json_array());
        CHECK;
    } while (0);

    do {
        const char *expected = "[{},{\"ikey\":false},\"some string\"]";
        struct JsonValue *val = json_array();
        json_array_unshift(val, json_string("some string"));
        struct JsonValue *inner_obj = json_object();
        json_object_insert(inner_obj, "ikey", json_false());
        json_array_unshift(val, inner_obj);
        json_array_unshift(val, json_object());
        CHECK;
    } while (0);

    do {
        const char *expected = "{\"a\":[],\"ia\":[null],\"key\":13}";
        struct JsonValue *val = json_object();
        json_object_insert(val, "key", json_number(13));
        json_object_insert(val, "a", json_array());
        struct JsonValue *inner_arr = json_array();
        json_array_unshift(inner_arr, json_null());
        json_object_insert(val, "ia", inner_arr);
        CHECK;
    } while (0);

    do {
        const char *expected = "{\"a\":{},\"io\":{\"ikey\":true},\"key\":13}";
        struct JsonValue *val = json_object();
        json_object_insert(val, "key", json_number(13));
        json_object_insert(val, "a", json_object());
        struct JsonValue *inner_obj = json_object();
        json_object_insert(inner_obj, "ikey", json_true());
        json_object_insert(val, "io", inner_obj);
        CHECK;
    } while (0);

    do {
        const char *expected = "{\"a\":{},\"key\":13}";
        struct JsonValue *val = json_parse(" { \"key\" :  13 } ", 17);
        OK_FATAL(val != NULL, "initial json is valid");
        json_object_insert(val, "a", json_object());
        CHECK;
    } while (0);
#undef CHECK
}

// same as test_write() but with json_serialize_pretty()
static void test_write_pretty(void)
{
#define CHECK                                                               \
    unsigned slen = strlen(expected);                                       \
    unsigned glen;                                                          \
    char *got = json_serialize_pretty(val, &glen, 3);                       \
    json_delete(val);                                                       \
    OK_FATAL(got != NULL, "got serialized value");                          \
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
        const char *expected = "[\n   \"some string\"\n]";
        struct JsonValue *val = json_array();
        json_array_unshift(val, json_string("some string"));
        CHECK;
    } while (0);

    do {
        const char *expected = "[\n   -13,\n   true,\n   \"some string\"\n]";
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
        const char *expected = "{\n   \"key\" : 13\n}";
        struct JsonValue *val = json_object();
        json_object_insert(val, "key", json_number(13));
        CHECK;
    } while (0);

    do {
        const char *expected = "{\n   \"key\" : \"value\",\n   \"key2\" : false,\n"
            "   \"something\" : -99\n}";
        struct JsonValue *val = json_object();
        json_object_insert(val, "key2", json_false());
        json_object_insert(val, "something", json_number(-99));
        json_object_insert(val, "key", json_string("value"));
        CHECK;
    } while (0);

    do {
        const char *expected = "[\n   [],\n   [\n      \"inner string\"\n   ],\n   \"some string\"\n]";
        struct JsonValue *val = json_array();
        json_array_unshift(val, json_string("some string"));
        struct JsonValue *inner_arr = json_array();
        json_array_unshift(inner_arr, json_string("inner string"));
        json_array_unshift(val, inner_arr);
        json_array_unshift(val, json_array());
        CHECK;
    } while (0);

    do {
        const char *expected = "[\n   {},\n   {\n      \"ikey\" : false\n   },\n"
            "   \"some string\"\n]";
        struct JsonValue *val = json_array();
        json_array_unshift(val, json_string("some string"));
        struct JsonValue *inner_obj = json_object();
        json_object_insert(inner_obj, "ikey", json_false());
        json_array_unshift(val, inner_obj);
        json_array_unshift(val, json_object());
        CHECK;
    } while (0);

    do {
        const char *expected = "{\n   \"a\" : [],\n   \"ia\" :\n      [\n"
            "         null\n      ],\n   \"key\" : 13\n}";
        struct JsonValue *val = json_object();
        json_object_insert(val, "key", json_number(13));
        json_object_insert(val, "a", json_array());
        struct JsonValue *inner_arr = json_array();
        json_array_unshift(inner_arr, json_null());
        json_object_insert(val, "ia", inner_arr);
        CHECK;
    } while (0);

    do {
        const char *expected = "{\n   \"a\" : {},\n   \"io\" :\n      {\n"
            "         \"ikey\" : true\n      },\n   \"key\" : 13\n}";
        struct JsonValue *val = json_object();
        json_object_insert(val, "key", json_number(13));
        json_object_insert(val, "a", json_object());
        struct JsonValue *inner_obj = json_object();
        json_object_insert(inner_obj, "ikey", json_true());
        json_object_insert(val, "io", inner_obj);
        CHECK;
    } while (0);

    do {
        const char *expected = "{\n   \"a\" : {},\n   \"key\" : 13\n}";
        struct JsonValue *val = json_parse(" { \"key\" :   13 } ", 17);
        OK_FATAL(val != NULL, "initial json is valid");
        json_object_insert(val, "a", json_object());
        CHECK;
    } while (0);
#undef CHECK
}

// have items of various types, shift them with a growing string to see
//  if the output buffer reallocation is handled correctly for each type
//  (the output buffer is incremented by 128 bytes when it's full)
static void test_realloc(void)
{
    const char *expected_start = "{\"a\":\"";
    const char *expected_end = "\",\"array\":[true,\"testing\",false,42,null],"
        "\"number\":-41.500000,\"object\":{\"key\":\"value\"}}";

    struct JsonValue *js = json_object();
    json_object_insert(js, "number", json_number(-41.5));
    struct JsonValue *o = json_object();
    json_object_insert(o, "key", json_string("value"));
    json_object_insert(js, "object", o);
    struct JsonValue *a = json_array();
    json_array_unshift(a, json_null());
    json_array_unshift(a, json_number(42));
    json_array_unshift(a, json_false());
    json_array_unshift(a, json_string("testing"));
    json_array_unshift(a, json_true());
    json_object_insert(js, "array", a);

    char aaa[400];
    for (unsigned i=0; i<400; i++) {
        if (i > 0) aaa[i-1] = 'a';
        aaa[i] = 0;
        json_object_insert(js, "a", json_string(aaa));

        unsigned j_len;
        char *j_str = json_serialize(js, &j_len);
        OK_FATAL(j_str != NULL, "got string");
        OK(j_len == strlen(expected_start) + strlen(expected_end) + i, "length");

        char expected[1024];
        strcpy(expected, expected_start);
        strcat(expected, aaa);
        strcat(expected, expected_end);

        //if (i == 3) printf("\nexpected: '%s'\ngot     : '%s'\n", expected, j_str);
        OK(strcmp(expected, j_str) == 0, "serialize match");
        free(j_str);
    }
    json_delete(js);
}

// same as test_realloc() but with json_serialize_pretty()
static void test_realloc_pretty(void)
{
    const char *expected_start = "{\n \"a\" : \"";
    const char *expected_end = "\",\n \"array\" :\n  [\n   true,\n   \"testing\",\n   false,\n   42,\n"
        "   null\n  ],\n \"number\" : -41.500000,\n \"object\" :\n  {\n   \"key\" : \"value\"\n  }\n}";

    struct JsonValue *js = json_object();
    json_object_insert(js, "number", json_number(-41.5));
    struct JsonValue *o = json_object();
    json_object_insert(o, "key", json_string("value"));
    json_object_insert(js, "object", o);
    struct JsonValue *a = json_array();
    json_array_unshift(a, json_null());
    json_array_unshift(a, json_number(42));
    json_array_unshift(a, json_false());
    json_array_unshift(a, json_string("testing"));
    json_array_unshift(a, json_true());
    json_object_insert(js, "array", a);

    char aaa[400];
    for (unsigned i=0; i<400; i++) {
        if (i > 0) aaa[i-1] = 'a';
        aaa[i] = 0;
        json_object_insert(js, "a", json_string(aaa));

        unsigned j_len;
        char *j_str = json_serialize_pretty(js, &j_len, 1);
        OK_FATAL(j_str != NULL, "got string");
        OK(j_len == strlen(expected_start) + strlen(expected_end) + i, "length %u != %lu",
                j_len, strlen(expected_start) + strlen(expected_end) + i);

        char expected[1024];
        strcpy(expected, expected_start);
        strcat(expected, aaa);
        strcat(expected, expected_end);

        //if (i == 3) printf("\nexpected: '%s'\ngot     : '%s'\n", expected, j_str);
        OK(strcmp(expected, j_str) == 0, "serialize match");
        free(j_str);
    }
    json_delete(js);
}


TEST_CASES = {
    {"Write", test_write},
    {"Write pretty", test_write_pretty},
    {"Realloc", test_realloc},
    {"Realloc pretty", test_realloc_pretty},
    {NULL, NULL}
};

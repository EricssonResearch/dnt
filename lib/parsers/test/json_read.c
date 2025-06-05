/*
 * Copyright (c) 2023 Miklós Máté
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "testing.h"

#include "json.h"
#include "parserutils.h"

#include <string.h>
#include <stdlib.h>

TEST_INIT("Json Read");

static void test_read_good(void)
{
// we do the memdup to get rid of the null-termination (test over-reading in valgrind)
#define TEST_TYPE(type_, s_)                                        \
    for (unsigned i=0; i<sizeof(s_)/sizeof((s_)[0]); i++) {         \
        unsigned _len = strlen((s_)[i]);                            \
        char *_js = (char*)u_memdup((s_)[i], _len);                 \
        char *error = NULL;                                         \
        struct JsonValue *j = json_parse(_js, _len, &error);        \
        free(_js);                                                  \
        OK(error == NULL, "got error '%s'", error);                 \
        OK_FATAL(j != NULL, "json string %u should be valid", i);   \
        OK_FATAL(j->type == type_, "type");

#define END_TEST_TYPE                                               \
        OK(json_delete(j) == NULL, "delete must return NULL");      \
    }

    const char *test_null[] = {"null", "   null  \n  "};
    TEST_TYPE(JSON_NULL, test_null)
    END_TEST_TYPE

    const char *test_true[] = {"true", " \t  true  \r "};
    TEST_TYPE(JSON_TRUE, test_true)
    END_TEST_TYPE

    const char *test_false[] = {"false", "    false       \n        "};
    TEST_TYPE(JSON_FALSE, test_false)
    END_TEST_TYPE

    const char *test_string[] = {"\"some string to test\"",
        "   \n\n\t\n        \"some string to test\"   \r   "};
    TEST_TYPE(JSON_STRING, test_string)
        OK(strcmp(j->v.string, "some string to test") == 0, "string '%s' differs", j->v.string);
    END_TEST_TYPE

    const char *test_unicode_string[] = {"\"árvíztűrő tükörfúrógép そして日本語\"",
        "    \n\"árvíztűrő tükörfúrógép そして日本語\" \t  "};
    TEST_TYPE(JSON_STRING, test_unicode_string)
        OK(strcmp(j->v.string, "árvíztűrő tükörfúrógép そして日本語") == 0, "string '%s' differs", j->v.string);
    END_TEST_TYPE

    const char *test_escaped_string[] = {"\"\\\"\\\\\\t\\b\\n\\r\\f\\/\"",
        "   \r  \"\\\"\\\\\\t\\b\\n\\r\\f\\/\"   "};
    TEST_TYPE(JSON_STRING, test_escaped_string)
        OK(strcmp(j->v.string, "\"\\\t\b\n\r\f/") == 0, "string '%s' differs", j->v.string);
    END_TEST_TYPE

    const char *test_escaped_u16[] = {"\"\\u00e1rv\\u00edzt\\u0171r\\u0151 t\\u00fck\\u00f6rf\\u00far\\u00f3g\\u00e9p \\ud83d\\udde8 \\u305d\\u3057\\u3066\\u65e5\\u672c\\u8a9e \\ud83d\\udd92\"",
        "\"\\u00E1rv\\u00EDzt\\u0171r\\u0151 t\\u00FCk\\u00F6rf\\u00FAr\\u00F3g\\u00E9p \\uD83D\\uDDE8 \\u305D\\u3057\\u3066\\u65E5\\u672C\\u8A9E \\uD83D\\uDD92\""};
    TEST_TYPE(JSON_STRING, test_escaped_u16)
        OK(strcmp(j->v.string, "árvíztűrő tükörfúrógép 🗨 そして日本語 🖒") == 0, "string '%s' differs", j->v.string);
    END_TEST_TYPE

    const char *test_num1[] = {"425", "     425    "};
    TEST_TYPE(JSON_NUMBER, test_num1)
        OK(j->v.number == 425, "number %.9f", j->v.number);
    END_TEST_TYPE

    const char *test_num2[] = {"-31", " \n\n    -31 \t\r   "};
    TEST_TYPE(JSON_NUMBER, test_num2)
        OK(j->v.number == -31, "number %.9f", j->v.number);
    END_TEST_TYPE

    const char *test_num3[] = {"211.2", " \n\n    211.2 \t\r   "};
    TEST_TYPE(JSON_NUMBER, test_num3)
        OK(j->v.number == 211.2, "number %.9f", j->v.number);
    END_TEST_TYPE

    const char *test_num4[] = {"-0.5", " \n\n    -0.5 \t\r   "};
    TEST_TYPE(JSON_NUMBER, test_num4)
        OK(j->v.number == -0.5, "number %.9f", j->v.number);
    END_TEST_TYPE

    const char *test_num_exp[] = {"-0.5E3", " \n\n    -0.5e3 \t\r   ", "-5000E-1  "};
    TEST_TYPE(JSON_NUMBER, test_num_exp)
        OK(j->v.number == -500.0, "number %.9f", j->v.number);
    END_TEST_TYPE

    const char *test_emptyarray[] = {"[]", "   [     ]     "};
    TEST_TYPE(JSON_ARRAY, test_emptyarray)
        OK(json_array_empty(j), "empty array");
    END_TEST_TYPE

    const char *test_arraybool[] = {"[false]", "  [   false   ]   "};
    TEST_TYPE(JSON_ARRAY, test_arraybool)
        OK(json_array_size(j) == 1, "array has one value");
        OK(json_array_at(j, 0)->type == JSON_FALSE, "array elem type");
    END_TEST_TYPE

    const char *test_arraystring[] = {"[\"false\"]", "   [ \n  \"false\"     ]  "};
    TEST_TYPE(JSON_ARRAY, test_arraystring)
        OK(json_array_size(j) == 1, "array has one value");
        OK(json_array_at(j, 0)->type == JSON_STRING, "array elem type");
        OK(strcmp(json_array_at(j, 0)->v.string, "false") == 0, "string '%s' differs", json_array_at(j, 0)->v.string);
    END_TEST_TYPE

    const char *test_arraymulti[] = {
        "[\"null\",-38,true]",
        "  [  \"null\"  ,  -38  ,  true  ]  "};
    TEST_TYPE(JSON_ARRAY, test_arraymulti)
        OK_FATAL(json_array_size(j) == 3, "size %u", json_array_size(j));
        OK(json_array_at(j, 0)->type == JSON_STRING, "first elem type");
        OK(strcmp(json_array_at(j, 0)->v.string, "null") == 0, "string '%s' differs", json_array_at(j, 0)->v.string);
        OK(json_array_at(j, 1)->type == JSON_NUMBER, "second elem type");
        OK(json_array_at(j, 1)->v.number == -38, "number %.9f", json_array_at(j, 1)->v.number);
        OK(json_array_at(j, 2)->type == JSON_TRUE,   "third elem type");
    END_TEST_TYPE

    const char *test_emptyobject[] = {"{}", "       {         }       "};
    TEST_TYPE(JSON_OBJECT, test_emptyobject)
        OK_FATAL(j->v.object != NULL, "object always has hashmap");
        OK(hashmap_count(j->v.object) == 0, "empty object");
    END_TEST_TYPE

    const char *test_object_one[] = {
        "{\"key\":\"value\"}",
        "   {  \"key\"   : \"value\"   }   "};
    TEST_TYPE(JSON_OBJECT, test_object_one)
        OK_FATAL(j->v.object != NULL, "object always has hashmap");
        OK(hashmap_count(j->v.object) == 1, "one item");
        struct JsonValue *val = json_object_get_string(j, "key");
        OK_FATAL(val != NULL, "have value");
        OK(strcmp(val->v.string, "value") == 0, "correct value '%s'", val->v.string);
    END_TEST_TYPE

    const char *test_object_three[] = {
        "{\"key\":\"value\",\"double\":-9.1,\"true\":true}",
        "  {   \"key\"   :   \"value\"   ,   \"double\" \n :  -9.1  ,  \"true\"  :  true  }  "};
    TEST_TYPE(JSON_OBJECT, test_object_three)
        OK_FATAL(j->v.object != NULL, "object always has hashmap");
        OK(hashmap_count(j->v.object) == 3, "three items");
        struct JsonValue *val = json_object_get_string(j, "key");
        OK_FATAL(val != NULL, "have value");
        OK(strcmp(val->v.string, "value") == 0, "correct value '%s'", val->v.string);
        val = json_object_get_number(j, "double");
        OK_FATAL(val != NULL, "have value");
        OK(val->v.number == -9.1, "correct value %.9f", val->v.number);
        val = json_object_get_true(j, "true");
        OK_FATAL(val != NULL, "have value");
    END_TEST_TYPE

    const char *test_object_escape[] = {"{\"\\\"\\\\\\t\\u00e1rv\\u00edzt\\u0171r\\u0151 \\ud83d\\udde8\" : \"\\u65E5\\u672C\\u8A9E\"}"};
    TEST_TYPE(JSON_OBJECT, test_object_escape)
        OK_FATAL(j->v.object != NULL, "object always has hashmap");
        OK(hashmap_count(j->v.object) == 1, "one item");
        struct JsonValue *val = json_object_get_string(j, "\"\\\tárvíztűrő 🗨");
        OK_FATAL(val != NULL, "have value");
        OK(strcmp(val->v.string, "日本語") == 0, "correct value '%s'", val->v.string);
    END_TEST_TYPE

    const char *test_object_duplicate[] = {"{\"key\":1,\"key\":2,\"key\":3}"};
    TEST_TYPE(JSON_OBJECT, test_object_duplicate)
        OK_FATAL(j->v.object != NULL, "object always has hashmap");
        OK(hashmap_count(j->v.object) == 1, "one item");
        struct JsonValue *val = json_object_get_number(j, "key");
        OK_FATAL(val != NULL, "have value");
        OK(val->v.number == 1, "correct value %.9f", val->v.number);
    END_TEST_TYPE

    const char *test_aina1[] = {"[[]]", "  [  [  ]  ]  "};
    TEST_TYPE(JSON_ARRAY, test_aina1)
        OK_FATAL(json_array_size(j) == 1, "size %u", json_array_size(j));
        OK_FATAL(json_array_at(j, 0)->type == JSON_ARRAY, "inner type");
        OK_FATAL(json_array_size(json_array_at(j, 0)) == 0, "inner size %u", json_array_size(j));
    END_TEST_TYPE

    const char *test_aina2[] = {"[[4],-1]", "  [  [ 4  ] , -1  ]  "};
    TEST_TYPE(JSON_ARRAY, test_aina2)
        OK_FATAL(json_array_size(j) == 2, "size %u", json_array_size(j));
        OK_FATAL(json_array_at(j, 0)->type == JSON_ARRAY, "inner type");
        OK_FATAL(json_array_size(json_array_at(j, 0)) == 1, "inner size %u", json_array_size(j));
        OK(json_array_at(json_array_at(j, 0), 0)->type == JSON_NUMBER, "inner elem type");
        OK(json_array_at(json_array_at(j, 0), 0)->v.number == 4, "inner number value %.9f",
                json_array_at(json_array_at(j, 0), 0)->v.number);
        OK_FATAL(json_array_at(j, 1)->type == JSON_NUMBER, "type");
        OK(json_array_at(j, 1)->v.number == -1, "number value %.9f", json_array_at(j, 1)->v.number);
    END_TEST_TYPE

    const char *test_oina1[] = {"[{}]", "  [  {  }  ]  "};
    TEST_TYPE(JSON_ARRAY, test_oina1)
        OK_FATAL(json_array_size(j) == 1, "size %u", json_array_size(j));
        OK_FATAL(json_array_at(j, 0)->type == JSON_OBJECT, "inner type");
        OK(json_object_empty(json_array_at(j, 0)), "object is empty");
    END_TEST_TYPE

    const char *test_oina2[] = {"[{\"k\":null},true]", "  [  { \"k\" : null  } , true  ]  "};
    TEST_TYPE(JSON_ARRAY, test_oina2)
        OK_FATAL(json_array_size(j) == 2, "size %u", json_array_size(j));
        OK_FATAL(json_array_at(j, 0)->type == JSON_OBJECT, "elem type");
        OK(json_object_count(json_array_at(j, 0)) == 1, "item count");
        struct JsonValue *val = json_object_get_null(json_array_at(j, 0), "k");
        OK_FATAL(val != NULL, "have value");
        OK(val->type == JSON_NULL, "null");
        OK_FATAL(json_array_at(j, 1)->type == JSON_TRUE, "elem type");
    END_TEST_TYPE

    const char *test_aino1[] = {"{\"k\":[6]}", " { \"k\" : [ 6 ] } "};
    TEST_TYPE(JSON_OBJECT, test_aino1)
        OK(json_object_count(j) == 1, "one item");
        struct JsonValue *val = json_object_get_array(j, "k");
        OK_FATAL(val != NULL, "have value");
        OK_FATAL(json_array_size(val) == 1, "one item");
        OK(json_array_at(val, 0)->type == JSON_NUMBER, "elem type");
        OK(json_array_at(val, 0)->v.number == 6, "correct value %.9f", json_array_at(val, 0)->v.number);
    END_TEST_TYPE

    const char *test_oino1[] = {"{\"k\":{\"m\":{}}}", " { \"k\" : { \"m\" : { } } } "};
    TEST_TYPE(JSON_OBJECT, test_oino1)
        OK(hashmap_count(j->v.object) == 1, "one item");
        struct JsonValue *val = json_object_get_object(j, "k");
        OK_FATAL(val != NULL, "have value");
        OK(hashmap_count(val->v.object) == 1, "one item");
        struct JsonValue *val2 = json_object_get_object(val, "m");
        OK_FATAL(val2 != NULL, "have value");
        OK(val2->type == JSON_OBJECT, "value type");
        OK(hashmap_count(val2->v.object) == 0, "empty");
    END_TEST_TYPE

#undef TEST_TYPE
#undef END_TEST_TYPE

    // here we also have a null termination at the end of the buffer
    const char *test_nullterminated = {"{\"key\": \"value\"}"};
    do {
        unsigned len = strlen(test_nullterminated)+1;
        char *js = (char*)u_memdup(test_nullterminated, len);
        char *error = NULL;
        struct JsonValue *j = json_parse(js, len, &error);
        free(js);
        OK(error == NULL, "got error '%s'", error);
        OK_FATAL(j != NULL, "json string should be valid");
        OK_FATAL(j->type == JSON_OBJECT, "type");
        OK(json_delete(j) == NULL, "delete must return NULL");
    } while (0);
}

static void test_read_bad(void)
{
    const char *strings[] = {
        "", "   \t \n  ", ",", "{", "}", "[", "]", ".",
        "null,", "null4", "[],", "{},",
        "0x1337", "5e", "5ee", "5ergs", "5e-", "e3",
        "3+4", "-", "-.", "+", "+.", "-.e", "+.e",
        "--3", "++3", ".3", "-.3", "3.", "-3.", "3.e3", "-3.e3",
        "0123", "-0123", "3e400", "0x123", "0xabc",
        "\"unterminated string",
        "\"unterminated string\\\"",
        "\"unterminated \\\" string",
        "\"unterminated \\",
        "\"unescaped \n control \t char\"",
        "\"undefined \\k control char\"",
        "\"\\u1\"", "\"\\u12\"", "\"\\u123\"",
        "\"\\u000g\"",
        "\"missing \\uD83D surrogate\"",
        "\"missing \\uDD92 surrogate\"",
        "\"missing \\uD83D\\uD83D surrogate\"",
        "\"missing surrogate \\uD83D\"",
        "\"missing surrogate \\uDD92\"",
        "\"missing surrogate \\uD83D\\uD83D\"",
        "True", "False", "NULL",
        "trues", "falses", "nulls",
        "[,]", "[55,]", "[1 2 3]", "[\"unterminated ]",
        "[ [ ]", "[ { ]", "[ { }",
        "{,}", "{:}", "{\"}",
        "{\"key\" : }", "{\"key\" \"value\"}",
        "{\"key\" : \"value\" , }",
        "{\"key   : \"value\" }",
        "{\"key\" : \"value   }",
        "{  key   : \"value\" }",
        "{\"unescaped \n control \t char\" : \"value\"}",
        "{\"undefined \\k control char\" : \"value\"}",
        "{\"\\u1\" : \"value\"}",
        "{\"\\u12\" : \"value\"}",
        "{\"\\u123\" : \"value\"}",
        "{\"\\u000g\" : \"value\"}",
        "{\"missing \\uD83D surrogate\" : \"value\"}",
        "{\"missing \\uDD92 surrogate\" : \"value\"}",
        "{\"missing \\uD83D\\uD83D surrogate\" : \"value\"}",
        "{\"missing surrogate \\uD83D\" : \"value\"}",
        "{\"missing surrogate \\uDD92\" : \"value\"}",
        "{\"missing surrogate \\uD83D\\uD83D\" : \"value\"}",
        "{\"key\" : [}",
        "{\"key\" : {}",
        "{\"key\" : {",
        "{32 : 4}", "{true : false}", "{false : true}",
    };

    for (unsigned i=0; i<sizeof(strings)/sizeof(strings[0]); i++) {
        unsigned len = strlen(strings[i]);
        char *js = (char*)u_memdup(strings[i], len);
        char *error;
        struct JsonValue *j = json_parse(js, len, &error);
        free(js);
        OK(j == NULL, "string %u '%s' should be invalid", i, strings[i]);
        OK(error != NULL, "string %u '%s' got no error string", i, strings[i]);
        //printf("json error '%s'\n", error);
        free(error);
    }

    // strings with \0 in the middle
    struct {
        const char *str;
        unsigned len;
    } nullstrings[] = {
        {" 42 \0  ", 7},
        {" 42 \0 x", 7},
        {" \"string\"\0 ", 11},
        {" \"string\0  ", 11},
        {" \"string\0\" ", 11},
        {"[1, \0]", 6},
        {"{\"x\":1\0}", 8},
        {"{\"x\0\":1}", 8},
    };

    for (unsigned i=0; i<sizeof(nullstrings)/sizeof(nullstrings[0]); i++) {
        char *js = (char*)u_memdup(nullstrings[i].str, nullstrings[i].len);
        char *error;
        struct JsonValue *j = json_parse(js, nullstrings[i].len, &error);
        free(js);
        OK(j == NULL, "string %u '%s' should be invalid", i, nullstrings[i].str);
        OK(error != NULL, "string %u '%s' got no error string", i, nullstrings[i].str);
        free(error);
    }
}

static void test_readback(void)
{
    const char *orig = "[[],[1],[1,2],null,true,false,\"string\",{},{\"key\":\"val\"},{\"k1\":42,\"k2\":false}]";
    unsigned len = strlen(orig);
    char *error = NULL;
    char *trim = (char*)u_memdup(orig, len); // trim null-termination
    struct JsonValue *j = json_parse(trim, len, &error);
    free(trim);
    OK_FATAL(j != NULL, "original string is valid");
    OK(error == NULL, "got error '%s'", error);
    char *pretty = json_serialize_pretty(j, &len, 5);
    json_delete(j);
    OK_FATAL(pretty != NULL, "pretty serialization ok");
    trim = (char*)u_memdup(pretty, len);
    free(pretty);
    j = json_parse(trim, len, &error);
    free(trim);
    OK_FATAL(j != NULL, "pretty string is valid");
    OK(error == NULL, "got error '%s'", error);
    char *terse = json_serialize(j, &len);
    json_delete(j);
    OK(strcmp(orig, terse) == 0, "orig '%s' terse '%s'", orig, terse);
    free(terse);
}

TEST_CASES = {
    {"read good", test_read_good},
    {"read bad", test_read_bad},
    {"readback", test_readback},
    {NULL, NULL}
};

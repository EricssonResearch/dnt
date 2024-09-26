
#include "testing.h"

#include "json.h"
#include "utils.h"

#include <string.h>
#include <stdlib.h>

TEST_INIT("Json Read");

static void test_read_good(void)
{
// we do the memdup to get rid of the null-termination (test over-reading in valgrind)
#define TEST_TYPE(s)                                        \
    for (unsigned i=0; (s)[i]; i++) {                       \
        unsigned _len = strlen((s)[i]);                     \
        char *_js = (char*)memdup((s)[i], _len);            \
        struct JsonValue *j = json_parse(_js, _len);        \
        free(_js);                                          \
        OK_FATAL(j != NULL, "json string %u should be valid", i);

    const char *test_null[] = {"null", "   null  \n  ", NULL};
    TEST_TYPE(test_null)
        OK(j->type == JSON_NULL, "type");
        OK(json_delete(j) == NULL, "delete must return NULL");
    }

    const char *test_true[] = {"true", " \t  true  \r ", NULL};
    TEST_TYPE(test_true)
        OK(j->type == JSON_TRUE, "type");
        OK(json_delete(j) == NULL, "delete must return NULL");
    }

    const char *test_false[] = {"false", "    false       \n        ", NULL};
    TEST_TYPE(test_false)
        OK(j->type == JSON_FALSE, "type");
        OK(json_delete(j) == NULL, "delete must return NULL");
    }

    const char *test_string[] = {"\"some string to test\"",
        "   \n\n\t\n        \"some string to test\"   \r   ", NULL};
    TEST_TYPE(test_string)
        OK(j->type == JSON_STRING, "type");
        OK(strcmp(j->v.string, "some string to test") == 0, "string '%s' differs", j->v.string);
        OK(json_delete(j) == NULL, "delete must return NULL");
    }

    const char *test_num1[] = {"425", "     425    ", NULL};
    TEST_TYPE(test_num1)
        OK(j->type == JSON_NUMBER, "type");
        OK(j->v.number == 425, "number %.9f", j->v.number);
        OK(json_delete(j) == NULL, "delete must return NULL");
    }

    const char *test_num2[] = {"-31", " \n\n    -31 \t\r   ", NULL};
    TEST_TYPE(test_num2)
        OK(j->type == JSON_NUMBER, "type");
        OK(j->v.number == -31, "number %.9f", j->v.number);
        OK(json_delete(j) == NULL, "delete must return NULL");
    }

    const char *test_num3[] = {"211.2", " \n\n    211.2 \t\r   ", NULL};
    TEST_TYPE(test_num3)
        OK(j->type == JSON_NUMBER, "type");
        OK(j->v.number == 211.2, "number %.9f", j->v.number);
        OK(json_delete(j) == NULL, "delete must return NULL");
    }

    const char *test_num4[] = {"-.5", " \n\n    -.5 \t\r   ", NULL};
    TEST_TYPE(test_num4)
        OK(j->type == JSON_NUMBER, "type");
        OK(j->v.number == -0.5, "number %.9f", j->v.number);
        OK(json_delete(j) == NULL, "delete must return NULL");
    }

    const char *test_emptyarray[] = {"[]", "   [     ]     ", NULL};
    TEST_TYPE(test_emptyarray)
        OK_FATAL(j->type == JSON_ARRAY, "type");
        OK(json_array_empty(j), "empty array");
        OK(json_delete(j) == NULL, "delete must return NULL");
    }

    const char *test_arraybool[] = {"[false]", "  [   false   ]   ", NULL};
    TEST_TYPE(test_arraybool)
        OK_FATAL(j->type == JSON_ARRAY, "type");
        OK(json_array_size(j) == 1, "array has one value");
        OK(json_array_at(j, 0)->type == JSON_FALSE, "array elem type");
        OK(json_delete(j) == NULL, "delete must return NULL");
    }

    const char *test_arraystring[] = {"[\"false\"]", "   [ \n  \"false\"     ]  ", NULL};
    TEST_TYPE(test_arraystring)
        OK_FATAL(j->type == JSON_ARRAY, "type");
        OK(json_array_size(j) == 1, "array has one value");
        OK(json_array_at(j, 0)->type == JSON_STRING, "array elem type");
        OK(strcmp(json_array_at(j, 0)->v.string, "false") == 0, "string '%s' differs", json_array_at(j, 0)->v.string);
        OK(json_delete(j) == NULL, "delete must return NULL");
    }

    const char *test_arraymulti[] = {
        "[\"null\",-38,true]",
        "  [  \"null\"  ,  -38  ,  true  ]  ", NULL};
    TEST_TYPE(test_arraymulti)
        OK_FATAL(j->type == JSON_ARRAY, "type");
        OK_FATAL(json_array_size(j) == 3, "size %u", json_array_size(j));
        OK(json_array_at(j, 0)->type == JSON_STRING, "first elem type");
        OK(strcmp(json_array_at(j, 0)->v.string, "null") == 0, "string '%s' differs", json_array_at(j, 0)->v.string);
        OK(json_array_at(j, 1)->type == JSON_NUMBER, "second elem type");
        OK(json_array_at(j, 1)->v.number == -38, "number %.9f", json_array_at(j, 1)->v.number);
        OK(json_array_at(j, 2)->type == JSON_TRUE,   "third elem type");
        OK(json_delete(j) == NULL, "delete must return NULL");
    }

    const char *test_emptyobject[] = {"{}", "       {         }       ", NULL};
    TEST_TYPE(test_emptyobject)
        OK_FATAL(j->type == JSON_OBJECT, "type");
        OK_FATAL(j->v.object != NULL, "object always has hashmap");
        OK(hashmap_count(j->v.object) == 0, "empty object");
        OK(json_delete(j) == NULL, "delete must return NULL");
    }

    const char *test_objectone[] = {
        "{\"key\":\"value\"}",
        "   {  \"key\"   : \"value\"   }   ", NULL};
    TEST_TYPE(test_objectone)
        OK_FATAL(j->type == JSON_OBJECT, "type");
        OK_FATAL(j->v.object != NULL, "object always has hashmap");
        OK(hashmap_count(j->v.object) == 1, "one item");
        struct JsonValue *val = json_object_get_string(j, "key");
        OK_FATAL(val != NULL, "have value");
        OK(strcmp(val->v.string, "value") == 0, "correct value '%s'", val->v.string);
        OK(json_delete(j) == NULL, "delete must return NULL");
    }

    const char *test_objectthree[] = {
        "{\"key\":\"value\",\"double\":-9.1,\"true\":true}",
        "  {   \"key\"   :   \"value\"   ,   \"double\" \n :  -9.1  ,  \"true\"  :  true  }  ", NULL};
    TEST_TYPE(test_objectthree)
        OK_FATAL(j->type == JSON_OBJECT, "type");
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
        OK(json_delete(j) == NULL, "delete must return NULL");
    }

    const char *test_aina1[] = {"[[]]", "  [  [  ]  ]  ", NULL};
    TEST_TYPE(test_aina1)
        OK_FATAL(j->type == JSON_ARRAY, "type");
        OK_FATAL(json_array_size(j) == 1, "size %u", json_array_size(j));
        OK_FATAL(json_array_at(j, 0)->type == JSON_ARRAY, "inner type");
        OK_FATAL(json_array_size(json_array_at(j, 0)) == 0, "inner size %u", json_array_size(j));
        OK(json_delete(j) == NULL, "delete must return NULL");
    }

    const char *test_aina2[] = {"[[4],-1]", "  [  [ 4  ] , -1  ]  ", NULL};
    TEST_TYPE(test_aina2)
        OK_FATAL(j->type == JSON_ARRAY, "type");
        OK_FATAL(json_array_size(j) == 2, "size %u", json_array_size(j));
        OK_FATAL(json_array_at(j, 0)->type == JSON_ARRAY, "inner type");
        OK_FATAL(json_array_size(json_array_at(j, 0)) == 1, "inner size %u", json_array_size(j));
        OK(json_array_at(json_array_at(j, 0), 0)->type == JSON_NUMBER, "inner elem type");
        OK(json_array_at(json_array_at(j, 0), 0)->v.number == 4, "inner number value %.9f",
                json_array_at(json_array_at(j, 0), 0)->v.number);
        OK_FATAL(json_array_at(j, 1)->type == JSON_NUMBER, "type");
        OK(json_array_at(j, 1)->v.number == -1, "number value %.9f", json_array_at(j, 1)->v.number);
        OK(json_delete(j) == NULL, "delete must return NULL");
    }

    const char *test_oina1[] = {"[{}]", "  [  {  }  ]  ", NULL};
    TEST_TYPE(test_oina1)
        OK_FATAL(j->type == JSON_ARRAY, "type");
        OK_FATAL(json_array_size(j) == 1, "size %u", json_array_size(j));
        OK_FATAL(json_array_at(j, 0)->type == JSON_OBJECT, "inner type");
        OK(json_object_empty(json_array_at(j, 0)), "object is empty");
        OK(json_delete(j) == NULL, "delete must return NULL");
    }

    const char *test_oina2[] = {"[{\"k\":null},true]", "  [  { \"k\" : null  } , true  ]  ", NULL};
    TEST_TYPE(test_oina2)
        OK_FATAL(j->type == JSON_ARRAY, "type");
        OK_FATAL(json_array_size(j) == 2, "size %u", json_array_size(j));
        OK_FATAL(json_array_at(j, 0)->type == JSON_OBJECT, "elem type");
        OK(json_object_count(json_array_at(j, 0)) == 1, "item count");
        struct JsonValue *val = json_object_get_null(json_array_at(j, 0), "k");
        OK_FATAL(val != NULL, "have value");
        OK(val->type == JSON_NULL, "null");
        OK_FATAL(json_array_at(j, 1)->type == JSON_TRUE, "elem type");
        OK(json_delete(j) == NULL, "delete must return NULL");
    }

    const char *test_aino1[] = {"{\"k\":[6]}", " { \"k\" : [ 6 ] } ", NULL};
    TEST_TYPE(test_aino1)
        OK_FATAL(j->type == JSON_OBJECT, "type");
        OK(json_object_count(j) == 1, "one item");
        struct JsonValue *val = json_object_get_array(j, "k");
        OK_FATAL(val != NULL, "have value");
        OK_FATAL(json_array_size(val) == 1, "one item");
        OK(json_array_at(val, 0)->type == JSON_NUMBER, "elem type");
        OK(json_array_at(val, 0)->v.number == 6, "correct value %.9f", json_array_at(val, 0)->v.number);
        OK(json_delete(j) == NULL, "delete must return NULL");
    }

    const char *test_oino1[] = {"{\"k\":{\"m\":{}}}", " { \"k\" : { \"m\" : { } } } ", NULL};
    TEST_TYPE(test_oino1)
        OK_FATAL(j->type == JSON_OBJECT, "type");
        OK(hashmap_count(j->v.object) == 1, "one item");
        struct JsonValue *val = json_object_get_object(j, "k");
        OK_FATAL(val != NULL, "have value");
        OK(hashmap_count(val->v.object) == 1, "one item");
        struct JsonValue *val2 = json_object_get_object(val, "m");
        OK_FATAL(val2 != NULL, "have value");
        OK(val2->type == JSON_OBJECT, "value type");
        OK(hashmap_count(val2->v.object) == 0, "empty");
        OK(json_delete(j) == NULL, "delete must return NULL");
    }
#undef TEST_TYPE

    // test null-terminated string
    do {
        const char *test_nullterm = " 42\0 this part is ignored";
        struct JsonValue *j = json_parse(test_nullterm, 10);
        OK_FATAL(j != NULL, "json string should be valid");
        OK(j->type == JSON_NUMBER, "type");
        OK(j->v.number == 42, "number %.9f", j->v.number);
        OK(json_delete(j) == NULL, "delete must return NULL");
    } while (0);
}

static void test_read_bad(void)
{
    const char *strings[] = {
        "", ",", "{", "}", "[", "]", ".",
        "null,", "null4", "[],", "{},",
        "\"unterminated string",
        "True", "False", "NULL",
        "3+4", "-", "-.",
        "[,]", "[55,]", "[\"unterminated ]",
        "[ [ ]", "[ { ]", "[ { }",
        "{,}", "{:}", "{\"}",
        "{\"key\" : }", "{\"key\" \"value\"}",
        "{\"key\" : \"value\" , }",
        "{\"key   : \"value\" }",
        "{\"key\" : \"value   }",
        "{  key   : \"value\" }",
        "{\"key\" : [}",
        "{\"key\" : {}",
        "{\"key\" : {",
        "{32 : 4}", "{true : false}", "{false : true}",
        NULL
    };

    for (unsigned i=0; strings[i]; i++) {
        unsigned len = strlen(strings[i]);
        char *js = (char*)memdup(strings[i], len);
        struct JsonValue *j = json_parse(js, len);
        free(js);
        OK(j == NULL, "string %u should be invalid", i);
    }
}

static void test_readback(void)
{
    const char *orig = "[[],[1],[1,2],null,true,false,\"string\",{},{\"key\":\"val\"},{\"k1\":42,\"k2\":false}]";
    unsigned len = strlen(orig);
    char *trim = (char*)memdup(orig, len); // trim null-termination
    struct JsonValue *j = json_parse(trim, len);
    free(trim);
    OK_FATAL(j != NULL, "original string is valid");
    char *pretty = json_serialize_pretty(j, &len, 5);
    json_delete(j);
    OK_FATAL(pretty != NULL, "pretty serialization ok");
    trim = (char*)memdup(pretty, len);
    free(pretty);
    j = json_parse(trim, len);
    free(trim);
    OK_FATAL(j != NULL, "pretty string is valid");
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

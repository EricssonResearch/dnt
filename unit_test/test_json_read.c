
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
        OK_FATAL(j != NULL, "json string should be valid");

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
        OK(j->v.array == NULL, "empty array");
        OK(json_delete(j) == NULL, "delete must return NULL");
    }

    const char *test_arraybool[] = {"[false]", "  [   false   ]   ", NULL};
    TEST_TYPE(test_arraybool)
        OK_FATAL(j->type == JSON_ARRAY, "type");
        OK_FATAL(j->v.array != NULL, "array has a value");
        OK_FATAL(j->v.array->val != NULL, "array has a valid value");
        OK(j->v.array->next == NULL, "array has one value");
        OK(j->v.array->val->type == JSON_FALSE, "array elem type");
        OK(json_delete(j) == NULL, "delete must return NULL");
    }

    const char *test_arraystring[] = {"[\"false\"]", "   [ \n  \"false\"     ]  ", NULL};
    TEST_TYPE(test_arraystring)
        OK_FATAL(j->type == JSON_ARRAY, "type");
        OK_FATAL(j->v.array != NULL, "array has a value");
        OK_FATAL(j->v.array->val != NULL, "array has a valid value");
        OK(j->v.array->next == NULL, "array has one value");
        OK(j->v.array->val->type == JSON_STRING, "array elem type");
        OK(strcmp(j->v.array->val->v.string, "false") == 0, "string '%s' differs", j->v.array->val->v.string);
        OK(json_delete(j) == NULL, "delete must return NULL");
    }

    const char *test_arraymulti[] = {
        "[\"null\",-38,true]",
        "  [  \"null\"  ,  -38  ,  true  ]  ", NULL};
    TEST_TYPE(test_arraymulti)
        OK_FATAL(j->type == JSON_ARRAY, "type");
        struct JsonArray *a = j->v.array;
        OK_FATAL(a != NULL, "array has first value");
        OK_FATAL(a->val != NULL, "valid first value");
        OK(a->val->type == JSON_STRING, "array elem type");
        OK(strcmp(a->val->v.string, "null") == 0, "string '%s' differs", a->val->v.string);
        a = a->next;
        OK_FATAL(a != NULL, "array has second value");
        OK_FATAL(a->val != NULL, "valid second value");
        OK(a->val->type == JSON_NUMBER, "array elem type");
        OK(a->val->v.number == -38, "second number %.9f", a->val->v.number);
        a = a->next;
        OK_FATAL(a != NULL, "array has third value");
        OK_FATAL(a->val != NULL, "valid third value");
        OK(a->val->type == JSON_TRUE, "array elem type");
        a = a->next;
        OK_FATAL(a == NULL, "no fourth third value");
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
        OK(j->type == JSON_ARRAY, "type");
        struct JsonArray *a = j->v.array;
        OK_FATAL(a != NULL, "array has a value");
        OK_FATAL(a->val != NULL, "array has a valid value");
        OK(a->next == NULL, "array has one value");
        OK(a->val->type == JSON_ARRAY, "array elem type");
        OK(a->val->v.array == NULL, "inner array is empty");
        OK(json_delete(j) == NULL, "delete must return NULL");
    }

    const char *test_aina2[] = {"[[4],-1]", "  [  [ 4  ] , -1  ]  ", NULL};
    TEST_TYPE(test_aina2)
        OK(j->type == JSON_ARRAY, "type");
        struct JsonArray *a = j->v.array;
        OK_FATAL(a != NULL, "array has a value");
        OK_FATAL(a->val != NULL, "array has a valid value");
        OK(a->val->type == JSON_ARRAY, "array elem type");
        struct JsonArray *ia = a->val->v.array;
        OK(ia != NULL, "inner array is not empty");
        OK(ia->val != NULL, "inner array has a valid value");
        OK(ia->val->type == JSON_NUMBER, "inner array elem type");
        OK(ia->val->v.number == 4, "correct value %.9f", ia->val->v.number);
        a = a->next;
        OK_FATAL(a != NULL, "array has a second value");
        OK_FATAL(a->val != NULL, "array has a valid value");
        OK(a->val->type == JSON_NUMBER, "iarray elem type");
        OK(a->val->v.number == -1, "correct value %.9f", a->val->v.number);
        OK(json_delete(j) == NULL, "delete must return NULL");
    }

    const char *test_oina1[] = {"[{}]", "  [  {  }  ]  ", NULL};
    TEST_TYPE(test_oina1)
        OK(j->type == JSON_ARRAY, "type");
        struct JsonArray *a = j->v.array;
        OK_FATAL(a != NULL, "array has a value");
        OK_FATAL(a->val != NULL, "array has a valid value");
        OK(a->val->type == JSON_OBJECT, "array elem type");
        OK(hashmap_count(a->val->v.object) == 0, "object is empty");
        OK(a->next == NULL, "no second item");
        OK(json_delete(j) == NULL, "delete must return NULL");
    }

    const char *test_oina2[] = {"[{\"k\":null},true]", "  [  { \"k\" : null  } , true  ]  ", NULL};
    TEST_TYPE(test_oina2)
        OK(j->type == JSON_ARRAY, "type");
        struct JsonArray *a = j->v.array;
        OK_FATAL(a != NULL, "array has a value");
        OK_FATAL(a->val != NULL, "array has a valid value");
        OK(a->val->type == JSON_OBJECT, "array elem type");
        OK(a->val->v.object != NULL, "object always has hashmap");
        OK(hashmap_count(a->val->v.object) == 1, "one item");
        struct JsonValue *val = json_object_get_null(a->val, "k");
        OK_FATAL(val != NULL, "have value");
        a = a->next;
        OK_FATAL(a != NULL, "array has a second value");
        OK_FATAL(a->val != NULL, "array has a valid second value");
        OK(a->val->type == JSON_TRUE, "value type");
        OK(a->next == NULL, "no third item");
        OK(json_delete(j) == NULL, "delete must return NULL");
    }

    const char *test_aino1[] = {"{\"k\":[6]}", " { \"k\" : [ 6 ] } ", NULL};
    TEST_TYPE(test_aino1)
        OK_FATAL(j->type == JSON_OBJECT, "type");
        OK_FATAL(j->v.object != NULL, "object always has hashmap");
        OK(hashmap_count(j->v.object) == 1, "one item");
        struct JsonValue *val = json_object_get_array(j, "k");
        OK_FATAL(val != NULL, "have value");
        struct JsonArray *a = val->v.array;
        OK_FATAL(a != NULL, "array has a value");
        OK_FATAL(a->val != NULL, "array has a valid value");
        OK(a->val->type == JSON_NUMBER, "array elem type");
        OK(a->val->v.number == 6, "correct value %.9f", a->val->v.number);
        OK(json_delete(j) == NULL, "delete must return NULL");
    }

    const char *test_oino1[] = {"{\"k\":{\"m\":{}}}", " { \"k\" : { \"m\" : { } } } ", NULL};
    TEST_TYPE(test_oino1)
        OK_FATAL(j->type == JSON_OBJECT, "type");
        OK_FATAL(j->v.object != NULL, "object always has hashmap");
        OK(hashmap_count(j->v.object) == 1, "one item");
        struct JsonValue *val = json_object_get_object(j, "k");
        OK_FATAL(val != NULL, "have value");
        OK_FATAL(val->v.object != NULL, "object always has hashmap");
        OK(hashmap_count(val->v.object) == 1, "one item");
        struct JsonValue *val2 = json_object_get_object(val, "m");
        OK_FATAL(val2 != NULL, "have value");
        OK(val2->type == JSON_OBJECT, "value type");
        OK_FATAL(val2->v.object != NULL, "object always has hashmap");
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

TEST_CASES = {
    {"Read Good", test_read_good},
    {"Read Bad", test_read_bad},
    {NULL, NULL}
};

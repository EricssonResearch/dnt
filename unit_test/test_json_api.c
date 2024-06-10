
#include "testing.h"

#include "json.h"
#include "utils.h"

#include <string.h>
#include <stdlib.h>

TEST_INIT("Json API");

static void test_create(void)
{
    struct JsonValue *js = json_null();
    OK_FATAL(js, "have value");
    OK_FATAL(js->type == JSON_NULL, "null");
    js = json_delete(js);
    OK_FATAL(js == NULL, "delete returns null");

    js = json_true();
    OK_FATAL(js, "have value");
    OK_FATAL(js->type == JSON_TRUE, "true");
    js = json_delete(js);
    OK_FATAL(js == NULL, "delete returns null");

    js = json_false();
    OK_FATAL(js, "have value");
    OK(js->type == JSON_FALSE, "false");
    js = json_delete(js);
    OK_FATAL(js == NULL, "delete returns null");

    js = json_number(4.2);
    OK_FATAL(js, "have value");
    OK_FATAL(js->type == JSON_NUMBER, "number");
    OK(js->v.number == 4.2, "value %.09f", js->v.number);
    js = json_delete(js);
    OK_FATAL(js == NULL, "delete returns null");

    char *str = strdup("jawa script");
    js = json_string(str); // this is supposed to make a copy of str
    free(str);
    OK_FATAL(js, "have value");
    OK_FATAL(js->type == JSON_STRING, "string");
    OK(strcmp(js->v.string, "jawa script") == 0, "value '%s'", js->v.string);
    js = json_delete(js);
    OK_FATAL(js == NULL, "delete returns null");
}

static void test_array(void)
{
    struct JsonValue *js = json_array();
    OK_FATAL(js, "have value");
    OK_FATAL(js->type == JSON_ARRAY, "array");
    OK(json_array_empty(js), "empty");
    OK(json_array_size(js) == 0, "empty");

    // push
    for (unsigned i=1; i<=10; i++) {
        json_array_push(js, json_number(i));
        OK(json_array_empty(js) == 0, "not empty");
        OK(json_array_size(js) == i, "size %u", json_array_size(js));
        for (unsigned j=0; j<i; j++) {
            struct JsonValue *a = json_array_at(js, j);
            OK(a->type == JSON_NUMBER, "type");
            OK(a->v.number == j+1, "j %u value %.9f", j, a->v.number);
        }
    }

    // pop
    for (unsigned i=0; i<10; i++) {
        struct JsonValue *p = json_array_pop(js);
        OK_FATAL(p != NULL, "popped value");
        OK_FATAL(p->type == JSON_NUMBER, "popped type");
        OK(p->v.number == 10-i, "i %u popped number %.9f", i, p->v.number);
        OK(json_delete(p) == NULL, "delete returns null");
    }
    OK_FATAL(json_array_pop(js) == NULL, "pop from empty array");

    // unshift
    for (unsigned i=1; i<=10; i++) {
        json_array_unshift(js, json_number(i));
        OK(json_array_empty(js) == 0, "not empty");
        OK(json_array_size(js) == i, "size %u", json_array_size(js));
        for (unsigned j=0; j<i; j++) {
            struct JsonValue *a = json_array_at(js, j);
            OK(a->type == JSON_NUMBER, "type");
            OK(a->v.number == i-j, "j %u value %.9f", j, a->v.number);
        }
    }

    // shift
    for (unsigned i=0; i<10; i++) {
        struct JsonValue *p = json_array_shift(js);
        OK_FATAL(p != NULL, "shifted value");
        OK_FATAL(p->type == JSON_NUMBER, "shifted type");
        OK(p->v.number == 10-i, "i %u shifted number %.9f", i, p->v.number);
        OK(json_delete(p) == NULL, "delete returns null");
    }
    OK_FATAL(json_array_shift(js) == NULL, "shift from empty array");

    js = json_delete(js);
    OK_FATAL(js == NULL, "delete returns null");

    // at and set
    js = json_array();
    OK_FATAL(js, "have value");
    OK_FATAL(js->type == JSON_ARRAY, "array");
    for (unsigned i=1; i<=10; i++) {
        json_array_unshift(js, json_number(i));
    }
    for (unsigned i=0; i<10; i++) {
        OK(json_array_at(js, i)->v.number == 10-i, "i %u number %.9f", i, json_array_at(js, i)->v.number);
    }
    for (unsigned i=0; i<10; i++) {
        json_array_set(js, i, json_number(i));
    }
    for (unsigned i=0; i<10; i++) {
        OK(json_array_at(js, i)->v.number == i, "i %u number %.9f", i, json_array_at(js, i)->v.number);
    }
    OK(json_array_at(js, 10) == NULL, "overindexing");
    json_array_set(js, 0, NULL);
    OK(json_array_at(js, 0)->v.number == 0, "untouched by NULL");
    struct JsonValue *tmp = json_true();
    json_array_set(js, 10, tmp);
    json_delete(tmp);
    OK(json_array_size(js) == 10, "untouched by overindexing");

    // delete non-empty array
    js = json_delete(js);
    OK_FATAL(js == NULL, "delete returns null");
}

static void test_object(void)
{
    struct JsonValue *js = json_object();
    OK_FATAL(js, "have value");
    OK_FATAL(js->type == JSON_OBJECT, "object");
    OK(json_object_empty(js), "empty");
    OK(json_object_count(js) == 0, "empty");

    json_object_insert(js, "null", json_null());
    json_object_insert(js, "true", json_true());
    json_object_insert(js, "false", json_false());
    json_object_insert(js, "number", json_number(42));
    json_object_insert(js, "string", json_string("characters"));
    json_object_insert(js, "array", json_array());
    json_object_insert(js, "object", json_object());
    OK(json_object_empty(js) == 0, "not empty");
    OK(json_object_count(js) == 7, "count %u", json_object_count(js));

    struct JsonValue *v;
    v = json_object_get_null(js, "null");
    OK(v != NULL, "null");
    v = json_object_get_null(js, "true");
    OK(v == NULL, "null");
    v = json_object_get_true(js, "true");
    OK(v != NULL, "true");
    v = json_object_get_true(js, "false");
    OK(v == NULL, "true");
    v = json_object_get_false(js, "false");
    OK(v != NULL, "false");
    v = json_object_get_false(js, "number");
    OK(v == NULL, "false");
    v = json_object_get_number(js, "number");
    OK(v != NULL, "number");
    v = json_object_get_number(js, "string");
    OK(v == NULL, "number");
    v = json_object_get_string(js, "string");
    OK(v != NULL, "string");
    v = json_object_get_string(js, "array");
    OK(v == NULL, "string");
    v = json_object_get_array(js, "array");
    OK(v != NULL, "array");
    v = json_object_get_array(js, "object");
    OK(v == NULL, "array");
    v = json_object_get_object(js, "object");
    OK(v != NULL, "object");
    v = json_object_get_object(js, "null");
    OK(v == NULL, "object");

    OK(json_object_remove(js, "no such key") == 0, "removed non-existing item");
    OK(json_object_count(js) == 7, "count %u", json_object_count(js));
    OK(json_object_remove(js, "false") != 0, "removed existing item");
    OK(json_object_count(js) == 6, "count %u", json_object_count(js));
    OK(json_object_remove(js, "false") == 0, "removed non-existing item");
    OK(json_object_count(js) == 6, "count %u", json_object_count(js));

    js = json_delete(js);
    OK_FATAL(js == NULL, "delete returns null");
}

TEST_CASES = {
    {"Create", test_create},
    {"Array", test_array},
    {"Object", test_object},
    {NULL, NULL}
};

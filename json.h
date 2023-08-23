
#ifndef JSON_H
#define JSON_H

#include "hashmap.h"

enum JsonType {
    JSON_NULL,
    JSON_STRING,
    JSON_NUMBER, //TODO separate int, float type?
    JSON_OBJECT,
    JSON_ARRAY,
    JSON_TRUE,
    JSON_FALSE,
};

struct JsonArray;

struct JsonValue {
    enum JsonType type;
    union {
        double number;
        char *string; // null-terminated
        struct HashMap *object; // never NULL
        struct JsonArray *array; // empty list is NULL
    } v;
};

struct JsonArray {
    struct JsonValue *val; // never NULL
    struct JsonArray *next;
};

// the string has to contain a single value in JSON format
// @returns NULL on failure
// the input @text doesn't have to be null-terminated
struct JsonValue *json_parse(const char *text, unsigned length);

// @returns NULL
struct JsonValue *json_delete(struct JsonValue *json);

// @returns a JSON string representation of @js
// @returns the length of the returned string as @length
// allocates the output buffer, the callee has to free it
// null-terminates the returned string, @length does not include the null-termination
char *json_serialize(const struct JsonValue *json, unsigned *length);

// helpers for creating stuff
struct JsonValue *json_null(void);
struct JsonValue *json_true(void);
struct JsonValue *json_false(void);
struct JsonValue *json_number(double n);
struct JsonValue *json_string(const char *s);
struct JsonValue *json_array(void);
struct JsonValue *json_object(void);

// inserts at the beginning
void json_array_unshift(struct JsonValue *array, struct JsonValue *value);

// @key is copied
void json_object_insert(struct JsonValue *object, const char *key, struct JsonValue *value);

void json_object_remove(struct JsonValue *object, const char *key);

// helpers for retrieving values, @returns NULL if type mismatches
struct JsonValue *json_object_get_null(struct JsonValue *object, const char *key);
struct JsonValue *json_object_get_true(struct JsonValue *object, const char *key);
struct JsonValue *json_object_get_false(struct JsonValue *object, const char *key);
struct JsonValue *json_object_get_bool(struct JsonValue *object, const char *key);
struct JsonValue *json_object_get_number(struct JsonValue *object, const char *key);
struct JsonValue *json_object_get_string(struct JsonValue *object, const char *key);
struct JsonValue *json_object_get_array(struct JsonValue *object, const char *key);
struct JsonValue *json_object_get_object(struct JsonValue *object, const char *key);
struct JsonValue *json_object_get_any(struct JsonValue *object, const char *key);

#endif // JSON_H


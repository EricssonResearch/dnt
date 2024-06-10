
#ifndef JSON_H
#define JSON_H

#include "hashmap.h"

//TODO support escaping/unescaping strings

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
        struct HashMap *object; // opaque, use the json_object_* methods
        struct JsonArray *array; // opaque, use the json_array_* methods
    } v; //TODO anonymous union is c11 or gnu99
};


// the string has to contain a single value in JSON format
// @returns NULL on failure
// the input @text doesn't have to be null-terminated
struct JsonValue *json_parse(const char *text, unsigned length);

// deletes the value, including all contained data
// @returns NULL
struct JsonValue *json_delete(struct JsonValue *json);

// @returns a JSON string representation of @js
// @returns the length of the returned string as @length
// the result is compact without any whitespace around the items
// the keys in objects are sorted alphabetically for reproducible results
// allocates the output buffer, the callee has to free it
// null-terminates the returned string, @length does not include the null-termination
char *json_serialize(const struct JsonValue *json, unsigned *length);

// @returns a JSON string representation of @js
// @returns the length of the returned string as @length
// the result is padded and indented with whitespaces
// @indent controls the depth of the indentation
// the keys in objects are sorted alphabetically for reproducible results
// allocates the output buffer, the callee has to free it
// null-terminates the returned string, @length does not include the null-termination
char *json_serialize_pretty(const struct JsonValue *json, unsigned *length, unsigned indent);

// helpers for creating values
struct JsonValue *json_null(void);
struct JsonValue *json_true(void);
struct JsonValue *json_false(void);
struct JsonValue *json_number(double n);
struct JsonValue *json_string(const char *s); // duplicates the given string
struct JsonValue *json_array(void);
struct JsonValue *json_object(void);

// array indexing for retrieval
// valid index @i are 0..size-1
// @returns NULL for invalid index
struct JsonValue *json_array_at(struct JsonValue *array, unsigned i);

// array indexing for replacing an existing value
// valid index @i are 0..size-1
// does nothing for invalid index or NULL @value
void json_array_set(struct JsonValue *array, unsigned i, struct JsonValue *value);

// removes and returns the first item
// @returns NULL when the array is empty
struct JsonValue *json_array_shift(struct JsonValue *array);

// inserts at the beginning
void json_array_unshift(struct JsonValue *array, struct JsonValue *value);

// removes and returns the last item
// @returns NULL when the array is empty
struct JsonValue *json_array_pop(struct JsonValue *array);

// inserts at the end
void json_array_push(struct JsonValue *array, struct JsonValue *value);

// @returns true if the array has no elements
int json_array_empty(const struct JsonValue *array);

// @returns the number of elements in the array
unsigned json_array_size(const struct JsonValue *array);

// @key is copied, @value is not
void json_object_insert(struct JsonValue *object, const char *key, struct JsonValue *value);

// the (key, value) pair in @object is deleted, @key is untouched
// @returns true if an item was removed
int json_object_remove(struct JsonValue *object, const char *key);

// @returns true if @object has no items
int json_object_empty(const struct JsonValue *object);

// @returns the item count in @object
int json_object_count(const struct JsonValue *object);

// helpers for retrieving values from objects, @returns NULL if type mismatches
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

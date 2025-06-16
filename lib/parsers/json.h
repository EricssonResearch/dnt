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

#ifndef JSON_H
#define JSON_H

/**
 * Read/write JSON data from/to strings.
 *
 * The reader doesn't assume the input string to be null-terminated.
 * The writer null-terminates the output string.
 *
 * Compatible with the ECMA-404 standard.
 */

#include "hashmap.h"

#ifdef __cplusplus
extern "C" {
#endif

#define JSON_MAX_RECURSION_DEPTH 50

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
        double number; // cannot be Inf or NaN
        char *string; // null-terminated, NULL means empty string
        struct HashMap *object; // opaque, use the json_object_* methods
        struct JsonArray *array; // opaque, use the json_array_* methods
    } v; //TODO anonymous union is c11 or gnu99
};


// @text has to contain a single value in JSON format
// the input @text doesn't have to be null-terminated
// if @text is null-terminated, @length must not include the null-termination
// @returns NULL on failure and sets @error to a dynamically allocated error string
struct JsonValue *json_parse(const char *text, unsigned length, char **error);

// deletes the value, including all contained data
// @returns NULL
struct JsonValue *json_delete(struct JsonValue *json);

// @returns a JSON string representation of @json
// @returns the length of the returned string as @length
// @returns NULL on memory allocation failure
// @returns NULL on invalid number value (infinity or NaN)
// the result is compact without any whitespace around the items
// the keys in objects are sorted alphabetically for reproducible results
// allocates the output buffer, the callee has to free it
// null-terminates the returned string, @length does not include the null-termination
char *json_serialize(const struct JsonValue *json, unsigned *length);

// @returns a JSON string representation of @json
// @returns the length of the returned string as @length
// @returns NULL on memory allocation failure
// @returns NULL on invalid number value (infinity or NaN)
// the result is padded and indented with whitespaces
// @indent controls the depth of the indentation
// the keys in objects are sorted alphabetically for reproducible results
// allocates the output buffer, the callee has to free it
// null-terminates the returned string, @length does not include the null-termination
char *json_serialize_pretty(const struct JsonValue *json, unsigned *length, unsigned indent);

#ifdef JSON_WANT_PRETTY_PRINT
// only include this if we really need it
#include <stdio.h>

// prints the JSON string representation of @json to @out
// @indent controls the depth of the indentation
// the keys in objects are sorted alphabetically for reproducible results
// the resulting string is not valid JSON!
// color scheme: string green, number blue, hash key cyan, bool yellow, null purple
void json_pretty_colorful_print(const struct JsonValue *json, FILE *out, unsigned indent);
#endif

// @returns true if the current locale is suitable
// unfortunately the printing/scanning of numbers depend on the locale
int json_check_locale(void);

// helpers for creating values
struct JsonValue *json_null(void);
struct JsonValue *json_true(void);
struct JsonValue *json_false(void);
struct JsonValue *json_bool(int n);
struct JsonValue *json_number(double n);
struct JsonValue *json_string(const char *s); // duplicates the given string
struct JsonValue *json_array(void);
struct JsonValue *json_object(void);

// @returns a deep copy of @json
struct JsonValue *json_duplicate(const struct JsonValue *json);

// array indexing for retrieval
// valid index @i are 0..size-1
// @returns NULL for invalid index
struct JsonValue *json_array_at(const struct JsonValue *array, unsigned i);

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
// @key cannot be NULL
// replaces any existing value with the same key
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

#ifdef __cplusplus
}
#endif

#endif // JSON_H

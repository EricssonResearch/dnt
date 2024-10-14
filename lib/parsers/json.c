
#include "json.h"
#include "parserutils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#define BUFFER_INCREMENT 128

#define calloc_struct(T) (struct T *)calloc(1, sizeof(struct T))

struct JsonArray {
    struct JsonValue **vals;
    unsigned num;
};


static int obj_delete_cb(const char *key, void *value, void *userdata)
{
    free((char*)key);
    struct JsonValue *val = (struct JsonValue *)value;
    json_delete(val);
    (void)userdata;
    return 1;
}

static char *get_string(const char *text, unsigned length, unsigned *i)
{
    if (text[*i] != '"') return NULL;

    *i += 1;
    unsigned len = 0;
    while (*i + len < length && text[*i + len] != '"') len++;
    if (*i + len == length) {
        fprintf(stderr, "json: at %d string is unterminated\n", *i);
        return NULL;
    }

    char *ret = u_strndup(text + *i, len);
    *i += len + 1;
    return ret;
}

static int is_numberchar(char c)
{
    if (isdigit(c)) return 1;
    if (c == '-' || c == '+' || c == 'e' || c == 'E' || c == '.') return 1;
    return 0;
}

static char *get_number_str(const char *text, unsigned length, unsigned *i)
{
    int nlen = 0;
    while (*i + nlen < length) {
        if (is_numberchar(text[*i + nlen])) {
            nlen++;
        } else {
            break;
        }
    }
    return u_strndup(text + *i, nlen);
}

static struct JsonValue *parse_value(const char *text, unsigned length, unsigned *i)
{
#define THROW(msg, ...)                                 \
    do {                                                \
        fprintf(stderr, "json: error at %d: " msg "\n", \
                *i, ##__VA_ARGS__);                     \
        return json_delete(ret);                        \
    } while (0)

#define SKIP_WS while (*i < length && isspace(text[*i])) *i += 1

    struct JsonValue *ret = NULL;
    SKIP_WS;
    if (*i == length) return NULL; // no value

    ret = calloc_struct(JsonValue);

    if (text[*i] == '{') {
        ret->type = JSON_OBJECT;
        ret->v.object = new_hashmap(13, obj_delete_cb, NULL);
        *i += 1;
        SKIP_WS;
        if (*i == length) THROW("object is unfinished");

        if (text[*i] == '}') { // empty object
            *i += 1;
            return ret;
        }

        while (1) {
            char *key = get_string(text, length, i);
            if (key == NULL)
                THROW("object key is invalid");

            SKIP_WS;
            if (*i == length) { free(key); THROW("object is unfinished"); }
            if (text[*i] != ':') { free(key); THROW("missing ':' in object"); }
            *i += 1;
            if (*i == length) { free(key); THROW("object is unfinished"); }
            SKIP_WS;
            if (*i == length) { free(key); THROW("object is unfinished"); }

            struct JsonValue *val = parse_value(text, length, i);
            if (val == NULL) { free(key); THROW("object value is invalid"); }
            hashmap_insert(ret->v.object, key, val);

            if (*i == length) THROW("object is unfinished");
            SKIP_WS;
            if (*i == length) THROW("object is unfinished");

            if (text[*i] == '}') {
                *i += 1;
                return ret;
            }

            if (text[*i] != ',')
                THROW("missing ',' from object");
            *i += 1;
            if (*i == length) THROW("object is unfinished");
            SKIP_WS;
            if (*i == length) THROW("object is unfinished");
        }
    } else if (text[*i] == '[') {
        ret->type = JSON_ARRAY;
        ret->v.array = calloc_struct(JsonArray);
        *i += 1;
        if (*i == length) THROW("array is unfinished");
        SKIP_WS;
        if (*i == length) THROW("array is unfinished");

        if (text[*i] == ']') { // empty array
            *i += 1;
            return ret;
        }

        while (1) {
            struct JsonValue *val = parse_value(text, length, i);
            if (val == NULL) {
                THROW("array item is invalid");
            }
            json_array_push(ret, val);

            SKIP_WS;
            if (*i == length) THROW("array is unfinished");
            if (text[*i] == ']') {
                *i += 1;
                return ret;
            }

            if (text[*i] != ',')
                THROW("array missing ','");
            *i += 1;
            if (*i == length) THROW("array is unfinished");
            SKIP_WS;
            if (*i == length) THROW("array is unfinished");
        };
    } else if (text[*i] == '"') {
        char *str = get_string(text, length, i);
        if (str == NULL)
            THROW("no closing quote for string");
        ret->type = JSON_STRING;
        ret->v.string = str;
    } else if ((length - *i >= 4) && strncmp(text+*i, "true", 4) == 0) {
        ret->type = JSON_TRUE;
        *i += 4;
    } else if ((length - *i >= 5) && strncmp(text+*i, "false", 5) == 0) {
        ret->type = JSON_FALSE;
        *i += 5;
    } else if ((length - *i >= 4) && strncmp(text+*i, "null", 4) == 0) {
        ret->type = JSON_NULL;
        *i += 4;
    } else {
        double num;
        int chars = 0;
        char *num_str = get_number_str(text, length, i);
        if (sscanf(num_str, "%lf%n", &num, &chars) != 1) {
            free(num_str);
            THROW("invalid character '%c' at position %d", text[*i], *i);
        }
        free(num_str);
        ret->type = JSON_NUMBER;
        ret->v.number = num;
        *i += chars;
    }

    return ret;
#undef THROW
#undef SKIP_WS
}

struct JsonValue *json_parse(const char *text, unsigned length)
{
    unsigned i = 0;

    struct JsonValue *ret = parse_value(text, length, &i);
    if (ret == NULL) {
        return NULL;
    }

    while (i < length && isspace(text[i])) i++;
    if (i < length && text[i] != 0) {
        fprintf(stderr, "json: extra character '%c' found after the JSON value\n", text[i]);
        return json_delete(ret);
    }

    return ret;
}

struct JsonValue *json_delete(struct JsonValue *json)
{
    if (!json) return NULL;

    switch (json->type) {
        case JSON_NULL:
        case JSON_TRUE:
        case JSON_FALSE:
        case JSON_NUMBER:
            break;
        case JSON_STRING:
            free(json->v.string);
            break;
        case JSON_OBJECT:
            delete_hashmap(json->v.object);
            break;
        case JSON_ARRAY:
            for (unsigned i=0; i<json->v.array->num; i++) {
                json_delete(json->v.array->vals[i]);
            }
            free(json->v.array->vals);
            free(json->v.array);
            break;
    }
    free(json);

    return NULL;
}

#define CHECK_BUF(n)                                    \
    while (*buflen - *slen < (n)) {                     \
        *buflen += BUFFER_INCREMENT;                    \
        char *_newbuf = (char *)realloc(buf, *buflen);  \
        if (_newbuf == NULL) {                          \
            free(buf);                                  \
            return NULL;                                \
        } else {                                        \
            buf = _newbuf;                              \
        }                                               \
    }

#define CHECK_BUF_OBJ(n)                                \
    while (*buflen - *slen < (n)) {                     \
        *buflen += BUFFER_INCREMENT;                    \
        char *_newbuf = (char *)realloc(buf, *buflen);  \
        if (_newbuf == NULL) {                          \
            free(buf);                                  \
            op->buf = NULL;                             \
            return 0;                                   \
        } else {                                        \
            buf = _newbuf;                              \
        }                                               \
    }

struct objparams {
    char *buf;
    unsigned *buflen;
    unsigned *slen;
    unsigned indent;
    unsigned increment;
};

static char *serialize_value(const struct JsonValue *json, char *buf, unsigned *buflen, unsigned *slen);

static int obj_print_cb(const char *key, void *value, void *userdata)
{
    const struct JsonValue *val = (const struct JsonValue *)value;
    struct objparams *op = (struct objparams *)userdata;
    char *buf = op->buf;
    unsigned *buflen = op->buflen;
    unsigned *slen = op->slen;

    unsigned c = strlen(key);
    CHECK_BUF_OBJ(c + 4);
    sprintf(buf+*slen, "\"%s\":", key);
    *slen += c + 3;

    buf = serialize_value(val, buf, buflen, slen);

    CHECK_BUF_OBJ(2);
    strcpy(buf+*slen, ",");
    *slen += 1;
    op->buf = buf;
    op->buflen = buflen;
    op->slen = slen;

    return 1;
}

static char *serialize_value(const struct JsonValue *json, char *buf, unsigned *buflen, unsigned *slen)
{
    switch (json->type) {
        case JSON_NULL:
            CHECK_BUF(5);
            strcpy(buf+*slen, "null");
            *slen += 4;
            break;
        case JSON_TRUE:
            CHECK_BUF(5);
            strcpy(buf+*slen, "true");
            *slen += 4;
            break;
        case JSON_FALSE:
            CHECK_BUF(6);
            strcpy(buf+*slen, "false");
            *slen += 5;
            break;
        case JSON_NUMBER: {
            const char *fmt = json->v.number == floor(json->v.number) ? "%.0f" : "%f";
            unsigned c = snprintf(NULL, 0, fmt, json->v.number);
            CHECK_BUF(c+1);
            sprintf(buf+*slen, fmt, json->v.number);
            *slen += c;
            break; }
        case JSON_STRING: {
            unsigned c = strlen(json->v.string);
            CHECK_BUF(c+3);
            sprintf(buf+*slen, "\"%s\"", json->v.string);
            *slen += c + 2;
            break; }
        case JSON_OBJECT: {
            CHECK_BUF(2);
            strcpy(buf+*slen, "{");
            *slen += 1;

            struct objparams op = { buf, buflen, slen, 0, 0 };
            if (hashmap_foreach_sorted(json->v.object, obj_print_cb, &op) == 0) {
                return NULL;
            }
            buf = op.buf;
            buflen = op.buflen;
            slen = op.slen;
            if (hashmap_count(json->v.object)) {
                // overwrite the last ','
                *slen -= 1;
                buf[*slen] = 0;
            }

            CHECK_BUF(2);
            strcpy(buf+*slen, "}");
            *slen += 1;
            break; }
        case JSON_ARRAY: {
            CHECK_BUF(2);
            strcpy(buf+*slen, "[");
            *slen += 1;

            for (unsigned i=0; i<json->v.array->num; i++) {
                buf = serialize_value(json->v.array->vals[i], buf, buflen, slen);

                if (i < json->v.array->num - 1) {
                    CHECK_BUF(2);
                    strcpy(buf+*slen, ",");
                    *slen += 1;
                }
            }

            CHECK_BUF(2);
            strcpy(buf+*slen, "]");
            *slen += 1;
            break; }
    }
    return buf;
}

char *json_serialize(const struct JsonValue *json, unsigned *length)
{
    char *buf = NULL;
    unsigned buflen = 0;
    unsigned len = 0;

    buf = serialize_value(json, buf, &buflen, &len);

    if (buf)
        *length = len;
    return buf;
}

static char *serialize_value_pretty(const struct JsonValue *json, char *buf, unsigned *buflen, unsigned *slen,
        unsigned indent, unsigned increment);

static int obj_print_pretty_cb(const char *key, void *value, void *userdata)
{
    const struct JsonValue *val = (const struct JsonValue *)value;
    struct objparams *op = (struct objparams *)userdata;
    char *buf = op->buf;
    unsigned *buflen = op->buflen;
    unsigned *slen = op->slen;

    // write simple values on the same line, compound values on new line with indent
    unsigned val_indent = 0;
    char after_colon = ' ';
    if ((val->type == JSON_ARRAY && !json_array_empty(val)) ||
            (val->type == JSON_OBJECT && hashmap_count(val->v.object))) {
        val_indent = op->indent + op->increment;
        after_colon = '\n';
    }
    unsigned c = strlen(key);
    CHECK_BUF_OBJ(op->indent + c + 6);
    sprintf(buf+*slen, "%*s\"%s\" :%c", op->indent, "", key, after_colon);
    *slen += op->indent + c + 5;

    buf = serialize_value_pretty(val, buf, buflen, slen, val_indent, op->increment);

    CHECK_BUF_OBJ(3);
    strcpy(buf+*slen, ",\n");
    *slen += 2;
    op->buf = buf;
    op->buflen = buflen;
    op->slen = slen;

    return 1;
}

static char *serialize_value_pretty(const struct JsonValue *json, char *buf, unsigned *buflen, unsigned *slen,
        unsigned indent, unsigned increment)
{
#define PRINT_INDENT                            \
    do {                                        \
        CHECK_BUF(indent+1);                    \
        sprintf(buf+*slen, "%*s", indent, "");  \
        *slen += indent;                        \
    } while (0)

    switch (json->type) {
        case JSON_NULL:
            PRINT_INDENT;
            CHECK_BUF(5);
            strcpy(buf+*slen, "null");
            *slen += 4;
            break;
        case JSON_TRUE:
            PRINT_INDENT;
            CHECK_BUF(5);
            strcpy(buf+*slen, "true");
            *slen += 4;
            break;
        case JSON_FALSE:
            PRINT_INDENT;
            CHECK_BUF(6);
            strcpy(buf+*slen, "false");
            *slen += 5;
            break;
        case JSON_NUMBER: {
            PRINT_INDENT;
            const char *fmt = json->v.number == floor(json->v.number) ? "%.0f" : "%f";
            unsigned c = snprintf(NULL, 0, fmt, json->v.number);
            CHECK_BUF(c+1);
            sprintf(buf+*slen, fmt, json->v.number);
            *slen += c;
            break; }
        case JSON_STRING: {
            PRINT_INDENT;
            unsigned c = strlen(json->v.string);
            CHECK_BUF(c+3);
            sprintf(buf+*slen, "\"%s\"", json->v.string);
            *slen += c + 2;
            break; }
        case JSON_OBJECT: {
            PRINT_INDENT;
            CHECK_BUF(2);
            strcpy(buf+*slen, "{");
            *slen += 1;

            if (hashmap_count(json->v.object)) {
                CHECK_BUF(2);
                strcpy(buf+*slen, "\n");
                *slen += 1;
            }

            struct objparams op = { buf, buflen, slen, indent+increment, increment };
            if (hashmap_foreach_sorted(json->v.object, obj_print_pretty_cb, &op) == 0) {
                return NULL;
            }
            buf = op.buf;
            buflen = op.buflen;
            slen = op.slen;

            if (hashmap_count(json->v.object)) {
                // overwrite the last ",\n" to "\n"
                *slen -= 1;
                buf[*slen-1] = '\n';
                buf[*slen] = 0;
                PRINT_INDENT;
            }

            CHECK_BUF(2);
            strcpy(buf+*slen, "}");
            *slen += 1;
            break; }
        case JSON_ARRAY: {
            PRINT_INDENT;
            CHECK_BUF(2);
            strcpy(buf+*slen, "[");
            *slen += 1;

            if (!json_array_empty(json)) {
                CHECK_BUF(2);
                strcpy(buf+*slen, "\n");
                *slen += 1;
            }

            for (unsigned i=0; i<json->v.array->num; i++) {
                buf = serialize_value_pretty(json->v.array->vals[i], buf, buflen, slen, indent+increment, increment);

                if (i < json->v.array->num - 1) {
                    CHECK_BUF(3);
                    strcpy(buf+*slen, ",\n");
                    *slen += 2;
                }
            }

            if (!json_array_empty(json)) {
                CHECK_BUF(2);
                strcpy(buf+*slen, "\n");
                *slen += 1;
                PRINT_INDENT;
            }

            CHECK_BUF(2);
            strcpy(buf+*slen, "]");
            *slen += 1;
            break; }
    }
    return buf;
#undef PRINT_INDENT
}

char *json_serialize_pretty(const struct JsonValue *json, unsigned *length, unsigned indent)
{
    char *buf = NULL;
    unsigned buflen = 0;
    unsigned len = 0;

    buf = serialize_value_pretty(json, buf, &buflen, &len, 0, indent);

    if (buf)
        *length = len;
    return buf;
}

struct JsonValue *json_null(void)
{
    struct JsonValue *ret = calloc_struct(JsonValue);
    ret->type = JSON_NULL;
    return ret;
}

struct JsonValue *json_true(void)
{
    struct JsonValue *ret = calloc_struct(JsonValue);
    ret->type = JSON_TRUE;
    return ret;
}

struct JsonValue *json_false(void)
{
    struct JsonValue *ret = calloc_struct(JsonValue);
    ret->type = JSON_FALSE;
    return ret;
}

struct JsonValue *json_number(double n)
{
    struct JsonValue *ret = calloc_struct(JsonValue);
    ret->type = JSON_NUMBER;
    ret->v.number = n;
    return ret;
}

struct JsonValue *json_string(const char *s)
{
    struct JsonValue *ret = calloc_struct(JsonValue);
    ret->type = JSON_STRING;
    ret->v.string = u_strdup(s);
    return ret;
}

struct JsonValue *json_array(void)
{
    struct JsonValue *ret = calloc_struct(JsonValue);
    ret->type = JSON_ARRAY;
    ret->v.array = calloc_struct(JsonArray);
    return ret;
}

struct JsonValue *json_object(void)
{
    struct JsonValue *ret = calloc_struct(JsonValue);
    ret->type = JSON_OBJECT;
    ret->v.object = new_hashmap(13, obj_delete_cb, NULL);
    return ret;
}


struct JsonValue *json_array_at(struct JsonValue *array, unsigned i)
{
    if (i >= json_array_size(array)) return NULL;
    return array->v.array->vals[i];
}

void json_array_set(struct JsonValue *array, unsigned i, struct JsonValue *value)
{
    if (i >= json_array_size(array)) return;
    if (value == NULL) return;
    json_delete(array->v.array->vals[i]);
    array->v.array->vals[i] = value;
}

struct JsonValue *json_array_shift(struct JsonValue *array)
{
    if (json_array_empty(array)) return NULL;
    struct JsonValue *ret = array->v.array->vals[0];
    array->v.array->num -= 1;
    struct JsonValue **nv = (struct JsonValue **)malloc(array->v.array->num * sizeof(struct JsonValue **));
    memcpy(nv, array->v.array->vals+1, array->v.array->num * sizeof(struct JsonValue *));
    free(array->v.array->vals);
    array->v.array->vals = nv;
    return ret;
}

void json_array_unshift(struct JsonValue *array, struct JsonValue *value)
{
    struct JsonValue **nv = (struct JsonValue **)malloc((array->v.array->num+1) * sizeof(struct JsonValue **));
    nv[0] = value;
    memcpy(nv+1, array->v.array->vals, array->v.array->num * sizeof(struct JsonValue *));
    array->v.array->num += 1;
    free(array->v.array->vals);
    array->v.array->vals = nv;
}

struct JsonValue *json_array_pop(struct JsonValue *array)
{
    if (json_array_empty(array)) return NULL;
    struct JsonValue *ret = array->v.array->vals[array->v.array->num-1];
    array->v.array->num -= 1;
    struct JsonValue **nv = (struct JsonValue **)malloc(array->v.array->num * sizeof(struct JsonValue **));
    memcpy(nv, array->v.array->vals, array->v.array->num * sizeof(struct JsonValue *));
    free(array->v.array->vals);
    array->v.array->vals = nv;
    return ret;
}

void json_array_push(struct JsonValue *array, struct JsonValue *value)
{
    struct JsonValue **nv = (struct JsonValue **)malloc((array->v.array->num+1) * sizeof(struct JsonValue **));
    memcpy(nv, array->v.array->vals, array->v.array->num * sizeof(struct JsonValue *));
    nv[array->v.array->num] = value;
    array->v.array->num += 1;
    free(array->v.array->vals);
    array->v.array->vals = nv;
}

int json_array_empty(const struct JsonValue *array)
{
    return json_array_size(array) == 0;
}

unsigned json_array_size(const struct JsonValue *array)
{
    return array->v.array->num;
}


void json_object_insert(struct JsonValue *object, const char *key, struct JsonValue *value)
{
    hashmap_insert(object->v.object, u_strdup(key), value);
}

int json_object_remove(struct JsonValue *object, const char *key)
{
    return hashmap_remove(object->v.object, key);
}

int json_object_empty(const struct JsonValue *object)
{
    return hashmap_count(object->v.object) == 0;
}

int json_object_count(const struct JsonValue *object)
{
    return hashmap_count(object->v.object);
}

struct JsonValue *json_object_get_null(struct JsonValue *object, const char *key)
{
    struct JsonValue *ret = (struct JsonValue *)hashmap_find(object->v.object, key);
    if (ret == NULL) return NULL;
    if (ret->type != JSON_NULL) return NULL;
    return ret;
}

struct JsonValue *json_object_get_true(struct JsonValue *object, const char *key)
{
    struct JsonValue *ret = (struct JsonValue *)hashmap_find(object->v.object, key);
    if (ret == NULL) return NULL;
    if (ret->type != JSON_TRUE) return NULL;
    return ret;
}

struct JsonValue *json_object_get_false(struct JsonValue *object, const char *key)
{
    struct JsonValue *ret = (struct JsonValue *)hashmap_find(object->v.object, key);
    if (ret == NULL) return NULL;
    if (ret->type != JSON_FALSE) return NULL;
    return ret;
}

struct JsonValue *json_object_get_bool(struct JsonValue *object, const char *key)
{
    struct JsonValue *ret = (struct JsonValue *)hashmap_find(object->v.object, key);
    if (ret == NULL) return NULL;
    if (ret->type == JSON_TRUE ) return ret;
    if (ret->type == JSON_FALSE) return ret;
    return NULL;
}

struct JsonValue *json_object_get_number(struct JsonValue *object, const char *key)
{
    struct JsonValue *ret = (struct JsonValue *)hashmap_find(object->v.object, key);
    if (ret == NULL) return NULL;
    if (ret->type != JSON_NUMBER) return NULL;
    return ret;
}

struct JsonValue *json_object_get_string(struct JsonValue *object, const char *key)
{
    struct JsonValue *ret = (struct JsonValue *)hashmap_find(object->v.object, key);
    if (ret == NULL) return NULL;
    if (ret->type != JSON_STRING) return NULL;
    return ret;
}

struct JsonValue *json_object_get_array(struct JsonValue *object, const char *key)
{
    struct JsonValue *ret = (struct JsonValue *)hashmap_find(object->v.object, key);
    if (ret == NULL) return NULL;
    if (ret->type != JSON_ARRAY) return NULL;
    return ret;
}

struct JsonValue *json_object_get_object(struct JsonValue *object, const char *key)
{
    struct JsonValue *ret = (struct JsonValue *)hashmap_find(object->v.object, key);
    if (ret == NULL) return NULL;
    if (ret->type != JSON_OBJECT) return NULL;
    return ret;
}

struct JsonValue *json_object_get_any(struct JsonValue *object, const char *key)
{
    return (struct JsonValue *)hashmap_find(object->v.object, key);
}

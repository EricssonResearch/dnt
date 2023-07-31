
#include "json.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#define BUFFER_INCREMENT 128

static int obj_delete_cb(const char *key, void *value, void *userdata)
{
    free((char*)key);
    struct JsonValue *val = value;
    json_delete(val);
    (void)userdata;
    return 1;
}

static char *get_string(const char *text, unsigned length, unsigned *i)
{
    if (text[*i] != '"') return NULL;

    *i += 1;
    if (*i == length) {
        fprintf(stderr, "json: at %d string is unterminated\n", *i);
        return NULL;
    }

    unsigned len = 0;
    while (*i + len < length && text[*i + len] != '"') len++;
    if (*i + len == length) {
        fprintf(stderr, "json: at %d string is unterminated\n", *i);
        return NULL;
    }

    //TODO handle the escape sequences
    char *ret = strndup(text + *i, len);
    *i += len + 1;
    return ret;
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
        if (*i == length) THROW("object is unfinished");
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
            json_array_unshift(ret, val);

            SKIP_WS;
            if (*i == length) THROW("array is unfinished");
            if (text[*i] == ']') {
                *i += 1;
                REVERSE_LIST(ret->v.array);
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
        //TODO sscanf() expects null-terminated input buffer
        //  must copy a substring that matches [0-9..eE] into a separate buffer, sscanf on that
        if (sscanf(text+*i, "%lf%n", &num, &chars) != 1) {
            THROW("invalid character '%c'", text[*i]);
        }
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
        case JSON_ARRAY: {
            struct JsonArray *a = json->v.array;
            while (a) {
                struct JsonArray *d = a;
                a = a->next;
                json_delete(d->val);
                free(d);
            }
            break; }
    }
    free(json);

    return NULL;
}


#define CHECK_BUF(n)                    \
    while (*buflen - *slen < (n)) {     \
        *buflen += BUFFER_INCREMENT;    \
        buf = realloc(buf, *buflen);    \
    }

struct objparams {
    char *buf;
    unsigned *buflen;
    unsigned *slen;
};

static char *serialize_value(const struct JsonValue *json, char *buf, unsigned *buflen, unsigned *slen);

static int obj_print_cb(const char *key, void *value, void *userdata)
{
    const struct JsonValue *val = value;
    struct objparams *op = userdata;
    char *buf = op->buf;
    unsigned *buflen = op->buflen;
    unsigned *slen = op->slen;

    unsigned c = strlen(key);
    CHECK_BUF(c+4);
    sprintf(buf+*slen, "\"%s\":", key);
    *slen += c + 3;

    buf = serialize_value(val, buf, buflen, slen);

    CHECK_BUF(2);
    strcat(buf+*slen, ",");
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
            sprintf(buf+*slen, "null");
            *slen += 4;
            break;
        case JSON_TRUE:
            CHECK_BUF(5);
            sprintf(buf+*slen, "true");
            *slen += 4;
            break;
        case JSON_FALSE:
            CHECK_BUF(6);
            sprintf(buf+*slen, "false");
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
            buf[*slen] = 0; // make sure it is terminated
            strcat(buf+*slen, "{");
            *slen += 1;

            struct objparams op = {buf, buflen, slen};
            hashmap_foreach_sorted(json->v.object, obj_print_cb, &op);
            buf = op.buf;
            buflen = op.buflen;
            slen = op.slen;
            if (hashmap_count(json->v.object)) {
                // overwrite the last ','
                *slen -= 1;
                buf[*slen] = 0;
            }

            CHECK_BUF(2);
            strcat(buf+*slen, "}");
            *slen += 1;
            break; }
        case JSON_ARRAY: {
            CHECK_BUF(2);
            buf[*slen] = 0; // make sure it is terminated
            strcat(buf+*slen, "[");
            *slen += 1;

            for (struct JsonArray *a = json->v.array; a; a = a->next) {
                buf = serialize_value(a->val, buf, buflen, slen);

                if (a->next) {
                    CHECK_BUF(2);
                    strcat(buf+*slen, ",");
                    *slen += 1;
                }
            }

            CHECK_BUF(2);
            strcat(buf+*slen, "]");
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
    ret->v.string = strdup(s);
    return ret;
}

struct JsonValue *json_array(void)
{
    struct JsonValue *ret = calloc_struct(JsonValue);
    ret->type = JSON_ARRAY;
    return ret;
}

struct JsonValue *json_object(void)
{
    struct JsonValue *ret = calloc_struct(JsonValue);
    ret->type = JSON_OBJECT;
    ret->v.object = new_hashmap(13, obj_delete_cb, NULL);
    return ret;
}

void json_array_unshift(struct JsonValue *array, struct JsonValue *value)
{
    struct JsonArray *a = calloc_struct(JsonArray);
    a->val = value;
    a->next = array->v.array;
    array->v.array = a;
}

void json_object_insert(struct JsonValue *object, const char *key, struct JsonValue *value)
{
    hashmap_insert(object->v.object, strdup(key), value);
}

void json_object_remove(struct JsonValue *object, const char *key)
{
    hashmap_remove(object->v.object, key);
}

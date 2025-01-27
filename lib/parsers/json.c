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

#include "json.h"
#include "parserutils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <locale.h>

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

static int get_hex(const char *text, unsigned count)
{
    int ret = 0;
    for (unsigned i=0; i<count; i++) {
        char c = text[i];
        ret <<= 4;
        if (c >= '0' && c <= '9') {
            ret += c - '0';
        } else if (c >= 'a' && c <= 'f') {
            ret += c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            ret += c - 'A' + 10;
        } else {
            return -1;
        }
    }
    return ret;
}

static unsigned write_utf8(char *text, int code)
{
    if (code <= 0x007f) {
        text[0] = code & 0x7f;
        return 1;
    } else if (code <= 0x07ff) {
        text[0] = 0xc0 + ((code >> 6) & 0x1f);
        text[1] = 0x80 + ((code >> 0) & 0x3f);
        return 2;
    } else if (code <= 0xffff) {
        text[0] = 0xe0 + ((code >> 12) & 0x0f);
        text[1] = 0x80 + ((code >>  6) & 0x3f);
        text[2] = 0x80 + ((code >>  0) & 0x3f);
        return 3;
    } else {
        text[0] = 0xf0 + ((code >> 18) & 0x07);
        text[1] = 0x80 + ((code >> 12) & 0x3f);
        text[2] = 0x80 + ((code >>  6) & 0x3f);
        text[3] = 0x80 + ((code >>  0) & 0x3f);
        return 4;
    }
}

static int utf16_high_surrogate(int u)
{
    return u >= 0xd800 && u <= 0xdbff;
}

static int utf16_low_surrogate(int u)
{
    return u >= 0xdc00 && u <= 0xdfff;
}

// @text is the start of the string, @length is the total length, @i is the current read position
static char *get_string(const char *text, unsigned length, unsigned *i, char **error)
{
#define THROW(msg, ...)                                 \
    do {                                                \
        *error = u_strdup_printf("at %d string " msg,   \
                *i + len, ##__VA_ARGS__);               \
        free(ret);                                      \
        return NULL;                                    \
    } while(0)

    if (text[*i] != '"') return NULL;

    *i += 1;

    char *ret = NULL;
    unsigned len = 0;
    int in_escape = 0;
    unsigned escapes = 0;
    while (*i + len < length) {
        if (in_escape) {
            in_escape = 0;
        } else {
            if (text[*i + len] == '\\') {
                in_escape = 1;
                escapes++;
            } else if (text[*i + len] == '"') {
                break;
            // note: iscntrl() is also true for 0x7f (DEL)
            } else if ((unsigned char)(text[*i + len]) <= 0x1f) {
                THROW("contains illegal control character");
            }
        }
        len++;
    }

    if (*i + len == length) {
        THROW("is unterminated");
    }
    if (in_escape) {
        THROW("has unfinished escape sequence");
    }

    if (escapes == 0) {
        ret = u_strndup(text + *i, len);
        *i += len + 1;
        return ret;
    }

    // here we assume that unescaping always shortens the string
    ret = (char*)malloc((len+1)*sizeof(char));

    in_escape = 0;
    unsigned ilen = 0;
    unsigned olen = 0;
    while (*i + ilen < length) {
        if (in_escape) {
            if (text[*i + ilen] == '"') {
                ret[olen++] = '"';
            } else if (text[*i + ilen] == '\\') {
                ret[olen++] = '\\';
            } else if (text[*i + ilen] == 'n') {
                ret[olen++] = '\n';
            } else if (text[*i + ilen] == 't') {
                ret[olen++] = '\t';
            } else if (text[*i + ilen] == 'r') {
                ret[olen++] = '\r';
            } else if (text[*i + ilen] == 'f') {
                ret[olen++] = '\f';
            } else if (text[*i + ilen] == 'b') {
                ret[olen++] = '\b';
            // our serialization doesn't escape '/' but others may
            } else if (text[*i + ilen] == '/') {
                ret[olen++] = '/';
            } else if (text[*i + ilen] == 'u') {
                if (*i + ilen + 4 < length) {
                    int u = get_hex(text + *i + ilen + 1, 4);
                    if (u < 0) {
                        THROW("unicode sequence invalid");
                    }

                    if (utf16_high_surrogate(u)) {
                        ilen += 4; // point to last hex
                        if (*i + ilen + 6 < length) {
                            if (text[*i + ilen + 1] == '\\' && text[*i + ilen + 2] == 'u') {
                                int v = get_hex(text + *i + ilen + 3, 4);
                                if (v < 0) {
                                    THROW("unicode low surrogate invalid");
                                }

                                if (utf16_low_surrogate(v)) {
                                    int w = ((u & 0x3ff) << 10) + (v & 0x3ff) + 0x10000;
                                    //printf("u %04x v %04x w %04x\n", u, v, w);
                                    olen += write_utf8(ret + olen, w);
                                    ilen += 6;
                                } else {
                                    THROW("unicode low surrogate is not low surrogate");
                                }
                            } else {
                                THROW("unicode surrogate sequence invalid");
                            }
                        } else {
                            THROW("unicode surrogate sequence incomplete");
                        }
                    } else if (utf16_low_surrogate(u)) {
                        THROW("unicode sequence starts with low surrogate");
                    } else {
                        olen += write_utf8(ret + olen, u);
                        ilen += 4; // point to last hex
                    }
                } else {
                    THROW("unicode sequence incomplete");
                }
            } else {
                THROW("undefined control sequence '\\%c'", text[*i + ilen]);
            }
            in_escape = 0;
            ilen++;
        } else {
            if (text[*i + ilen] == '\\') {
                in_escape = 1;
            } else if (text[*i + ilen] == '"') {
                break;
            } else {
                ret[olen++] = text[*i + ilen];
            }
            ilen++;
        }
    }
    ret[olen] = 0;

    if (olen * 2 < ilen) {
        char *newret = (char*)realloc(ret, (olen+1)*sizeof(char));
        if (newret)
            ret = newret;
    }

    *i += ilen + 1;
    return ret;
#undef THROW
}

// @text is the start of the string, @length is the total length, @i is the current read position
// returns a newly allocated string, or NULL if the format is invalid
// we need this validation because JSON number format is stricter than strtod()
static char *get_number_str(const char *text, unsigned length, unsigned *i)
{
    int nlen = 0;
    int had_digit = 0;
    int first_digit_0 = 0;

#define HAVE_INPUT (*i + nlen < length)

    while (HAVE_INPUT && isspace(text[*i + nlen]))
        nlen++;

    if (!HAVE_INPUT)
        return NULL;

    // must start with '-' or digit
    if (text[*i + nlen] == '-') {
    } else if (isdigit(text[*i + nlen])) {
        had_digit = 1;
        first_digit_0 = text[*i + nlen] == '0';
    } else {
        return NULL;
    }
    nlen++;

    // integral part
    while (HAVE_INPUT && isdigit(text[*i + nlen])) {
        if (!had_digit) {
            first_digit_0 = text[*i + nlen] == '0';
        }
        had_digit++;
        nlen++;
    }

    if (!had_digit)
        return NULL;

    if (had_digit > 1 && first_digit_0)
        return NULL;

    // optional fractional part
    if (HAVE_INPUT && text[*i + nlen] == '.') {
        nlen++;
        had_digit = 0;
        while (HAVE_INPUT && isdigit(text[*i + nlen])) {
            had_digit++;
            nlen++;
        }

        // there must be at least 1 fractional digit
        if (had_digit == 0)
            return NULL;
    }

    // optional exponent
    if (HAVE_INPUT && (text[*i + nlen] == 'e' || text[*i + nlen] == 'E')) {
        nlen++;
        int had_exponent_digit = 0;

        if (!HAVE_INPUT)
            return NULL;

        if (text[*i + nlen] == '-' || text[*i + nlen] == '+') {
        } else if (isdigit(text[*i + nlen])) {
            had_exponent_digit = 1;
        } else {
            return NULL;
        }
        nlen++;

        while (HAVE_INPUT && isdigit(text[*i + nlen])) {
            had_exponent_digit++;
            nlen++;
        }

        if (had_exponent_digit == 0)
            return NULL;
    }

    return u_strndup(text + *i, nlen);
#undef HAVE_INPUT
}

// @text is the start of the string, @length is the total length, @i is the current read position, @depth is recursion depth
static struct JsonValue *parse_value(const char *text, unsigned length, unsigned *i, unsigned depth, char **error)
{
#define THROW(msg, ...)                             \
    do {                                            \
        if (*error == NULL) {                       \
            *error = u_strdup_printf("at %d: " msg, \
                    *i, ##__VA_ARGS__);             \
        }                                           \
        return json_delete(ret);                    \
    } while (0)

#define SKIP_WS while (*i < length && isspace(text[*i])) *i += 1

    struct JsonValue *ret = NULL;
    if (depth > JSON_MAX_RECURSION_DEPTH) THROW("maximum recursion depth reached");

    SKIP_WS;
    if (*i == length) {
        *error = u_strdup("empty string is not valid");
        return NULL;
    }

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
            char *key = get_string(text, length, i, error);
            if (key == NULL)
                THROW("object key is invalid");

            SKIP_WS;
            if (*i == length) { free(key); THROW("object is unfinished"); }
            if (text[*i] != ':') { free(key); THROW("missing ':' in object"); }
            *i += 1;
            if (*i == length) { free(key); THROW("object is unfinished"); }
            SKIP_WS;
            if (*i == length) { free(key); THROW("object is unfinished"); }

            struct JsonValue *val = parse_value(text, length, i, depth+1, error);
            if (val == NULL) { free(key); THROW("object value is invalid"); }
            hashmap_insert(ret->v.object, key, val);

            SKIP_WS;
            if (*i == length) THROW("object is unfinished");

            if (text[*i] == '}') {
                *i += 1;
                return ret;
            }

            if (text[*i] != ',')
                THROW("missing ',' from object");
            *i += 1;

            SKIP_WS;
            if (*i == length) THROW("object is unfinished");
        }
    } else if (text[*i] == '[') {
        ret->type = JSON_ARRAY;
        ret->v.array = calloc_struct(JsonArray);
        *i += 1;

        SKIP_WS;
        if (*i == length) THROW("array is unfinished");

        if (text[*i] == ']') { // empty array
            *i += 1;
            return ret;
        }

        while (1) {
            struct JsonValue *val = parse_value(text, length, i, depth+1, error);
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

            SKIP_WS;
            if (*i == length) THROW("array is unfinished");
        };
    } else if (text[*i] == '"') {
        char *str = get_string(text, length, i, error);
        if (str == NULL)
            THROW("string is invalid");
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
        int chars = 0;
        char *num_str = get_number_str(text, length, i);

        if (num_str == NULL) {
            THROW("invalid number");
        }

        char *num_end;
        double num = strtod(num_str, &num_end);

        if (!isfinite(num)) {
            free(num_str);
            THROW("invalid number");
        }

        if (num_end == num_str) {
            free(num_str);
            THROW("invalid character '%c'", text[*i]);
        }
        chars = num_end - num_str;
        free(num_str);

        ret->type = JSON_NUMBER;
        ret->v.number = num;
        *i += chars;
    }

    return ret;
#undef THROW
#undef SKIP_WS
}

struct JsonValue *json_parse(const char *text, unsigned length, char **error)
{
    unsigned i = 0;
    *error = NULL;

    struct JsonValue *ret = parse_value(text, length, &i, 0, error);
    if (ret == NULL) {
        return NULL;
    }

    while (i < length && isspace(text[i])) i++;
    if (i < length && text[i] != 0) {
        *error = u_strdup_printf("extra character '%c' found after the JSON value", text[i]);
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

static char *serialize_number(double number, char *buf, unsigned *buflen, unsigned *slen)
{
    if (isinf(number) || isnan(number)) {
        free(buf);
        return NULL;
    }

    unsigned chars = snprintf(NULL, 0, "%.17g", number);
    CHECK_BUF(chars+1);
    sprintf(buf+*slen, "%.17g", number);
    *slen += chars;
    return buf;
}

static char *serialize_string(const char *string, char *buf, unsigned *buflen, unsigned *slen)
{
    CHECK_BUF(2);
    buf[*slen] = '\"';
    *slen += 1;

    // note: '/' can also be escaped (important in "</script>")

    for (const char *c=string; *c; c++) {
        // note: iscntrl() is also true for 0x7f (DEL) but we don't have to escape it
        if (*(unsigned char*)c <= 0x1f) {
            if (*c == '\n') {
                CHECK_BUF(2);
                buf[*slen] = '\\';
                buf[*slen+1] = 'n';
                *slen += 2;
            } else if (*c == '\t') {
                CHECK_BUF(2);
                buf[*slen] = '\\';
                buf[*slen+1] = 't';
                *slen += 2;
            } else if (*c == '\r') {
                CHECK_BUF(2);
                buf[*slen] = '\\';
                buf[*slen+1] = 'r';
                *slen += 2;
            } else if (*c == '\f') {
                CHECK_BUF(2);
                buf[*slen] = '\\';
                buf[*slen+1] = 'f';
                *slen += 2;
            } else if (*c == '\b') {
                CHECK_BUF(2);
                buf[*slen] = '\\';
                buf[*slen+1] = 'b';
                *slen += 2;
            } else {
                CHECK_BUF(7);
                unsigned u = *(unsigned char*)c;
                sprintf(buf+*slen, "\\u%04x", u);
                *slen += 6;
            }
        } else if (*c == '"') {
            CHECK_BUF(2);
            buf[*slen] = '\\';
            buf[*slen+1] = '\"';
            *slen += 2;
        } else if (*c == '\\') {
            CHECK_BUF(2);
            buf[*slen] = '\\';
            buf[*slen+1] = '\\';
            *slen += 2;
        } else {
            CHECK_BUF(1);
            buf[*slen] = *c;
            *slen += 1;
        }
    }

    CHECK_BUF(2);
    buf[*slen] = '\"';
    buf[*slen+1] = 0;
    *slen += 1;

    return buf;
}

static char *serialize_value(const struct JsonValue *json, char *buf, unsigned *buflen, unsigned *slen);

static int obj_print_cb(const char *key, void *value, void *userdata)
{
    const struct JsonValue *val = (const struct JsonValue *)value;
    struct objparams *op = (struct objparams *)userdata;
    char *buf = op->buf;
    unsigned *buflen = op->buflen;
    unsigned *slen = op->slen;

    buf = serialize_string(key, buf, buflen, slen);

    CHECK_BUF_OBJ(1);
    buf[*slen] = ':';
    *slen += 1;

    buf = serialize_value(val, buf, buflen, slen);
    if (buf == NULL) {
        return 0;
    }

    CHECK_BUF_OBJ(2);
    buf[*slen+0] = ',';
    buf[*slen+1] = 0;
    *slen += 1;
    op->buf = buf;

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
            buf = serialize_number(json->v.number, buf, buflen, slen);
            break; }
        case JSON_STRING:
            buf = serialize_string(json->v.string, buf, buflen, slen);
            break;
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
                if (buf == NULL) {
                    return NULL;
                }

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

    CHECK_BUF_OBJ(op->indent);
    for (unsigned i=0; i<op->indent; i++)
        buf[*slen + i] = ' ';
    *slen += op->indent;
    buf = serialize_string(key, buf, buflen, slen);
    CHECK_BUF_OBJ(3);
    buf[*slen+0] = ' ';
    buf[*slen+1] = ':';
    buf[*slen+2] = after_colon;
    *slen += 3;

    buf = serialize_value_pretty(val, buf, buflen, slen, val_indent, op->increment);
    if (buf == NULL) {
        return 0;
    }

    CHECK_BUF_OBJ(3);
    buf[*slen+0] = ',';
    buf[*slen+1] = '\n';
    buf[*slen+2] = 0;
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
        for (unsigned i_=0; i_<indent; i_++)    \
            buf[*slen+i_] = ' ';                \
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
            buf = serialize_number(json->v.number, buf, buflen, slen);
            break; }
        case JSON_STRING: {
            PRINT_INDENT;
            buf = serialize_string(json->v.string, buf, buflen, slen);
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
                if (buf == NULL) {
                    return NULL;
                }

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

int json_check_locale(void)
{
    struct lconv *lc = localeconv();
    if (strcmp(lc->decimal_point, ".") != 0) return 0;
    if (strcmp(lc->thousands_sep, "") != 0) return 0;
    return 1;
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

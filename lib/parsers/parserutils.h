/*
 * Copyright (c) 2022 Miklós Máté
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

#ifndef PARSER_UTILS_H
#define PARSER_UTILS_H

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

// returns a newly allocated duplicate of the memory pointed by @src
static inline
    __attribute__((warn_unused_result))
    __attribute__((nonnull(1)))
void *u_memdup(const void *src, unsigned size)
{
    void *ret = malloc(size);
    memcpy(ret, src, size);
    return ret;
}

// strdup is not in C99
// unlike the real strdup() this one @returns NULL if @s is NULL
static inline
    __attribute__((warn_unused_result))
char *u_strdup(const char *s)
{
    if (s == NULL) return NULL;
    unsigned len = strlen(s) + 1;
    char *ret = (char *)malloc(len*sizeof(char));
    memcpy(ret, s, len);
    return ret;
}

// strndup is not in C99
// unlike the real strndup() this one @returns NULL if @s is NULL
// also, it doesn't assume @s to be 0-terminated when it's longer than @maxlen
static inline
    __attribute__((warn_unused_result))
char *u_strndup(const char *s, unsigned maxlen)
{
    if (s == NULL) return NULL;
    unsigned slen = 0;
    while (slen < maxlen && s[slen] != 0) slen++;
    unsigned len = slen < maxlen ? slen : maxlen;
    char *ret = (char *)malloc((len+1)*sizeof(char));
    memcpy(ret, s, len);
    ret[len] = 0;
    return ret;
}

// returns a newly allocated string that is the concatenation of @s1 and @s2
// if @s1 is NULL, returns a copy of @s2
// if @s2 is NULL, returns a copy of @s1
// if both are NULL, returns NULL
static inline
    __attribute__((warn_unused_result))
char *u_strdupcat(const char *s1, const char *s2)
{
    if (s1 == NULL) return u_strdup(s2);
    if (s2 == NULL) return u_strdup(s1);
    unsigned l1 = strlen(s1);
    unsigned l2 = strlen(s2);
    char *ret = (char *)malloc((l1+l2+1)*sizeof(char));
    memcpy(ret, s1, l1);
    memcpy(ret+l1, s2, l2+1);
    return ret;
}

// creates a new string, and prints into it according to @format
// returns pointer to the newly created string
// the format string cannot be NULL
static inline
    __attribute__((format(printf, 1, 2)))
    __attribute__((warn_unused_result))
    __attribute__((nonnull(1)))
char *u_strdup_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int err = vsnprintf(NULL, 0, format, args);
    va_end(args);
    if (err < 0) {
        return NULL;
    }
    int length = err+1;
    char *ret = (char*)malloc(length*sizeof(char));
    va_start(args, format);
    err = vsnprintf(ret, length, format, args);
    va_end(args);
    if (err < 0) {
        free(ret);
        return NULL;
    }
    return ret;
}

#endif // PARSER_UTILS_H

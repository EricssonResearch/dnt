/*
 * Copyright (c) 2021 Miklós Máté
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

#ifndef INI_UTILS_H
#define INI_UTILS_H

#include <string.h>
#include <stdlib.h>

// unlike the real strdup() this one @returns NULL if @s is NULL
static inline char *u_strdup(const char *s)
{
    if (s == NULL) return NULL;
    unsigned len = strlen(s) + 1;
    char *ret = malloc(len*sizeof(char));
    memcpy(ret, s, len);
    return ret;
}

// returns a newly allocated string that is the concatenation of @s1 and @s2
// if @s1 is NULL, returns a copy of @s2
// if @s2 is NULL, returns a copy of @s1
// if both are NULL, returns NULL
static inline char *u_strcat(const char *s1, const char *s2)
{
    if (s1 == NULL) return u_strdup(s2);
    if (s2 == NULL) return u_strdup(s1);
    unsigned l1 = strlen(s1);
    unsigned l2 = strlen(s2);
    char *ret = malloc((l1+l2+1)*sizeof(char));
    memcpy(ret, s1, l1);
    memcpy(ret+l1, s2, l2+1);
    return ret;
}


#endif // INI_UTILS_H

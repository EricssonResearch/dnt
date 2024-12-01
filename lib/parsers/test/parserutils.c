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

#include "testing.h"

#include "parserutils.h"

#include <stdlib.h>
#include <string.h>

TEST_INIT("Parser Utils");

static void test_memdup(void)
{
    unsigned onstack[10];
    for (unsigned i=0; i<10; i++) onstack[i] = i;
    unsigned *dup = (unsigned*)u_memdup(onstack, 10*sizeof(unsigned));
    OK_FATAL(dup != NULL, "have dup");
    OK(dup != onstack, "new allocation");
    for (unsigned i=0; i<10; i++) {
        OK(dup[i] == onstack[i], "dup %u onstack %u", dup[i], onstack[i]);
    }
    free(dup);

    unsigned *onheap = (unsigned*)malloc(10*sizeof(unsigned));
    for (unsigned i=0; i<10; i++) onheap[i] = i;
    dup = (unsigned*)u_memdup(onheap, 10*sizeof(unsigned));
    OK_FATAL(dup != NULL, "have dup");
    OK(dup != onheap, "new allocation");
    for (unsigned i=0; i<10; i++) {
        OK(dup[i] == onheap[i], "dup %u onheap %u", dup[i], onheap[i]);
    }
    free(dup);
    free(onheap);
}

static void test_strdup(void)
{
    const char *str = "elephant";
    char *dup = u_strdup(str);
    OK_FATAL(dup != NULL, "have string");
    OK(dup != str, "new string");
    OK(strcmp(dup, str) == 0, "same content");
    free(dup);
    const char *pattern = "zebra %s %d";
    dup = u_strdup(pattern);
    OK_FATAL(dup != NULL, "have string");
    OK(dup != pattern, "new string");
    OK(strcmp(dup, pattern) == 0, "same content");
    free(dup);
    dup = u_strdup(NULL);
    OK(dup == NULL, "null");
}

static void test_strndup(void)
{
    const char *str = "elephant";
    char *dup = u_strndup(str, 8);
    OK_FATAL(dup != NULL, "have string");
    OK(dup != str, "new string");
    OK(strlen(dup) == 8, "properly terminated");
    OK(strcmp(dup, str) == 0, "good content");
    free(dup);
    dup = u_strndup(str, 4);
    OK_FATAL(dup != NULL, "have string");
    OK(dup != str, "new string");
    OK(strcmp(dup, "elep") == 0, "good content");
    free(dup);
    dup = u_strndup(str, 14);
    OK_FATAL(dup != NULL, "have string");
    OK(dup != str, "new string");
    OK(strcmp(dup, str) == 0, "good content");
    free(dup);

    char *unterminated = (char*)u_memdup(str, 8);
    OK_FATAL(unterminated != NULL, "have unterminated");
    dup = u_strndup(unterminated, 8);
    OK_FATAL(dup != NULL, "have string");
    OK(dup != str, "new string");
    OK(dup != unterminated, "new string");
    OK(strlen(dup) == 8, "properly terminated");
    OK(strcmp(dup, str) == 0, "good content");
    free(dup);
    dup = u_strndup(unterminated, 4);
    OK_FATAL(dup != NULL, "have string");
    OK(dup != str, "new string");
    OK(dup != unterminated, "new string");
    OK(strcmp(dup, "elep") == 0, "good content");
    free(dup);
    free(unterminated);
}

static void test_strdupcat(void)
{
    const char *s1 = "str";
    const char *s2 = " cat";

    char *dup = u_strdupcat(s1, s2);
    OK_FATAL(dup != NULL, "have string");
    OK(strcmp(dup, "str cat") == 0, "cat '%s'", dup);
    free(dup);
    dup = u_strdupcat(s1, NULL);
    OK_FATAL(dup != NULL, "have string");
    OK(strcmp(dup, s1) == 0, "s1");
    OK(dup != s1, "new string");
    free(dup);
    dup = u_strdupcat(NULL, s2);
    OK_FATAL(dup != NULL, "have string");
    OK(strcmp(dup, s2) == 0, "s2");
    OK(dup != s2, "new string");
    free(dup);
    OK(u_strdupcat(NULL, NULL) == NULL, "null");
}

static void test_strdup_printf(void)
{
    const char *pattern = "zebra %s %d";
    char *dup = u_strdup_printf(pattern, "stripe count", 31);
    OK_FATAL(dup != NULL, "have string");
    OK(dup != pattern, "new string");
    OK(strcmp(dup, "zebra stripe count 31") == 0, "printf '%s'", dup);
    char *dup2 = u_strdup_printf(pattern, "population", 784);
    OK_FATAL(dup2 != NULL, "have string");
    OK(dup2 != pattern, "new string");
    OK(dup2 != dup, "new string");
    OK(strcmp(dup2, "zebra population 784") == 0, "printf '%s'", dup2);
    free(dup);
    free(dup2);
}


TEST_CASES = {
    {"memdup", test_memdup},
    {"strdup", test_strdup},
    {"strndup", test_strndup},
    {"strdupcat", test_strdupcat},
    {"strdup_printf", test_strdup_printf},
    {NULL, NULL}
};


// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "utils.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

void *memdup(const void *src, unsigned size)
{
    void *dst = malloc(size);
    memcpy(dst, src, size);
    return dst;
}

char *strdup_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int err = vsnprintf(NULL, 0, format, args);
    va_end(args);
    if (err < 0) {
        return NULL;
    }
    int length = err+1;
    char *ret = (char *)malloc(length*sizeof(char));
    va_start(args, format);
    err = vsnprintf(ret, length, format, args);
    va_end(args);
    if (err < 0) {
        free(ret);
        return NULL;
    }
    return ret;
}


#include "utils.h"

#include <stdlib.h>
#include <string.h>

void *memdup(const void *src, size_t size)
{
    void *dst = malloc(size);
    memcpy(dst, src, size);
    return dst;
}

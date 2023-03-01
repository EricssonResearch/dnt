
#include "utils.h"

#include <stdlib.h>
#include <string.h>

void *memdup(const void *src, unsigned size)
{
    void *dst = malloc(size);
    memcpy(dst, src, size);
    return dst;
}


#ifndef R2_UTILS_H
#define R2_UTILS_H

#include <stddef.h>

#define calloc_struct(T) (struct T *)calloc(1, sizeof(struct T))

void *memdup(const void *src, size_t size);

#endif // R2_UTILS_H

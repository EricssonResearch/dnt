
#ifndef R2_UTILS_H
#define R2_UTILS_H

#define calloc_struct(T) (struct T *)calloc(1, sizeof(struct T))

#define calloc_struct_array(T, n) (struct T *)calloc(n, sizeof(struct T))

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

void *memdup(const void *src, unsigned size);

#endif // R2_UTILS_H

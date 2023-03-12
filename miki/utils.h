
#ifndef R2_UTILS_H
#define R2_UTILS_H

#define calloc_struct(T) (struct T *)calloc(1, sizeof(struct T))

#define calloc_struct_array(T, n) (struct T *)calloc((n), sizeof(struct T))

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

// integer division with rounding up
#define DIVCEIL(x, y) (((x) + (y) - 1) / (y))

#define REVERSE_LIST(list)              \
    do {                                \
        typeof(*list) *_newlist = NULL; \
        while (list) {                  \
            typeof(*list) *_l = list;   \
            list = list->next;          \
            _l->next = _newlist;        \
            _newlist = _l;              \
        }                               \
        list = _newlist;                \
    } while (0)

void *memdup(const void *src, unsigned size);

#endif // R2_UTILS_H

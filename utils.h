// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef DNT_UTILS_H
#define DNT_UTILS_H

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

// @src cannot be NULL!
void *memdup(const void *src, unsigned size) __attribute__((nonnull(1)));

// creates a new string, and prints into it according to @format
// returns pointer to the newly created string
// the format string cannot be NULL
char *strdup_printf(const char *format, ...)
    __attribute__((format(printf, 1, 2)))
    __attribute__((nonnull(1)));


#endif // DNT_UTILS_H

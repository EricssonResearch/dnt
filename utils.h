// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_UTILS_H
#define R2_UTILS_H

#include <time.h>
#include <stdint.h>

#define NSEC_PER_SEC 1000000000L

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
void *memdup(const void *src, unsigned size);

// timespec manipulator helpers
// source: https://sourceware.org/git/?p=glibc.git;a=blob;f=sysdeps/pthread/posix-timer.h
static inline int
timespec_compare (const struct timespec *left, const struct timespec *right)
{
    if (left->tv_sec < right->tv_sec)
        return -1;
    if (left->tv_sec > right->tv_sec)
        return 1;

    if (left->tv_nsec < right->tv_nsec)
        return -1;
    if (left->tv_nsec > right->tv_nsec)
        return 1;

    return 0;
}

static inline void
timespec_add (struct timespec *sum, const struct timespec *left,
        const struct timespec *right)
{
    sum->tv_sec = left->tv_sec + right->tv_sec;
    sum->tv_nsec = left->tv_nsec + right->tv_nsec;

    if (sum->tv_nsec >= NSEC_PER_SEC)
    {
        ++sum->tv_sec;
        sum->tv_nsec -= NSEC_PER_SEC;
    }
}

static inline void
timespec_sub (struct timespec *diff, const struct timespec *left,
        const struct timespec *right)
{
    diff->tv_sec = left->tv_sec - right->tv_sec;
    diff->tv_nsec = left->tv_nsec - right->tv_nsec;

    if (diff->tv_nsec < 0)
    {
        --diff->tv_sec;
        diff->tv_nsec += NSEC_PER_SEC;
    }
}

// Be careful if using this few billion years after 1970
static inline void
timespec_to_u64(const struct timespec *src, uint64_t *dst)
{
    *dst = (uint64_t)src->tv_sec * NSEC_PER_SEC + src->tv_nsec;
}

static inline void
timespec_from_u64(struct timespec *dst, const uint64_t src)
{
    dst->tv_nsec = src % NSEC_PER_SEC;
    dst->tv_sec = src / NSEC_PER_SEC;
}


#endif // R2_UTILS_H

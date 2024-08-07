// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


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

// @src cannot be NULL!
void *memdup(const void *src, unsigned size) __attribute__((nonnull(1)));

// creates a new string, and prints into it according to @format
// returns pointer to the newly created string
// the format string cannot be NULL
char *strdup_printf(const char *format, ...)
    __attribute__((format(printf, 1, 2)))
    __attribute__((nonnull(1)));

// iterate over the hashmap but dont use @cb
// @_valuetype is the type of the objects stored in the hashmap
// Two local scoped variables provided by this macro:
//   const char *key //hashmap key
//   _valuetype value //hashmap value for that key
// TODO: HashMap version check compile-time and error on mismatch
#define hashmap_foreach_nocb(_hash, _valuetype)                                                                                               \
    for(unsigned i = 0; i <= 0;)                                                                                                              \
    for(const char *key = NULL; i <= 0;)                                                                                                      \
    for(_valuetype *value = NULL; i <= 0;)                                                                                                    \
        for (                                                                                                                                 \
            struct _HashMap {                                                                                                                 \
                unsigned bucketcount;                                                                                                         \
                unsigned elemcount;                                                                                                           \
                struct _HashBucket {                                                                                                          \
                    const char *key;                                                                                                          \
                    void *value;                                                                                                              \
                    struct _HashBucket *next;                                                                                                 \
                } *buckets;                                                                                                                   \
                void *unused_delete_cb;                                                                                                       \
                void *userdata;                                                                                                               \
            } *_hm = (struct _HashMap *)_hash; i < _hm->bucketcount; ++i                                                                      \
        )                                                                                                                                     \
            if (_hm->buckets[i].key)                                                                                                          \
                for (                                                                                                                         \
                    struct _HashBucket {                                                                                                      \
                        const char *key;                                                                                                      \
                        void *value;                                                                                                          \
                        struct _HashBucket *next;                                                                                             \
                    } *_hb = (struct _HashBucket *)&_hm->buckets[i]; _hb && (key=_hb->key, value=(_valuetype *)_hb->value); _hb = _hb->next   \
                )


#endif // R2_UTILS_H

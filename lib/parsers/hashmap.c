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

#include "hashmap.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct HashBucket {
    const char *key;
    void *value;
    struct HashBucket *next;
};

struct HashMap {
    unsigned bucketcount;
    unsigned elemcount;
    struct HashBucket *buckets;
    hashmap_cb *item_delete_cb;
    void *userdata;
};


/* This is called djb2 hash by Daniel Bernstein.
 * The constants are magic numbers. */
static unsigned djb2_hash(const char *str)
{
    unsigned hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; // hash*33 + c
    }
    return hash;
}

static int default_item_remove_cb(const char *key, void *value, void *userdata)
{
    free((char*)key); //XXX ugly
    free(value);
    (void)userdata;
    return 1;
}

struct HashMap *new_hashmap(unsigned bucketcount, hashmap_cb *item_delete_cb, void *userdata)
{
    if (bucketcount == 0) return NULL;

    struct HashMap *ret = (struct HashMap *)malloc(sizeof(struct HashMap));

    ret->bucketcount = bucketcount;
    ret->elemcount = 0;
    ret->buckets = (struct HashBucket *)calloc(bucketcount, sizeof(struct HashBucket));
    if (item_delete_cb) {
        ret->item_delete_cb = item_delete_cb;
        ret->userdata = userdata;
    } else {
        ret->item_delete_cb = default_item_remove_cb;
        ret->userdata = NULL;
    }

    return ret;
}

struct HashMap *delete_hashmap(struct HashMap *hash)
{
    if (!hash) return NULL;

    for (unsigned i=0; i<hash->bucketcount; i++) {
        if (hash->buckets[i].key) {
            hash->item_delete_cb(hash->buckets[i].key, hash->buckets[i].value, hash->userdata);
            struct HashBucket *b = hash->buckets[i].next;
            while (b) {
                struct HashBucket *bn = b->next;
                hash->item_delete_cb(b->key, b->value, hash->userdata);
                free(b);
                b = bn;
            }
        }
    }
    free(hash->buckets);
    free(hash);
    return NULL;
}

int hashmap_insert(struct HashMap *hash, char *key, void *value)
{
    if (!hash) return 0;
    if (!key) return 0;

    unsigned h = djb2_hash(key) % hash->bucketcount;

    if (hash->buckets[h].key) {
        struct HashBucket *b = hash->buckets + h;
        int stop = 0;
        do {
            if (strcmp(b->key, key) == 0) {
                // override existing value
                hash->item_delete_cb(b->key, b->value, hash->userdata);
                b->key = key;
                b->value = value;
                return 0;
            }
            if (b->next)
                b = b->next;
            else
                stop = 1;
        } while (!stop);

        // append to the list
        b->next = (struct HashBucket *)malloc(sizeof(struct HashBucket));
        struct HashBucket *bn = b->next;
        bn->key = key;
        bn->value = value;
        bn->next = NULL;
        hash->elemcount++;
    } else {
        // the bucket was empty
        hash->buckets[h].key = key;
        hash->buckets[h].value = value;
        hash->buckets[h].next = NULL;
        hash->elemcount++;
    }
    return 1;
}

int hashmap_remove(struct HashMap *hash, const char *key)
{
    if (!hash) return 0;
    if (!key) return 0;

    unsigned h = djb2_hash(key) % hash->bucketcount;

    if (hash->buckets[h].key) {
        if (strcmp(hash->buckets[h].key, key) == 0) {
            // the item to be deleted is the first in the bucket
            hash->item_delete_cb(hash->buckets[h].key, hash->buckets[h].value, hash->userdata);
            if (hash->buckets[h].next) {
                // move the first in the chain into the bucket
                struct HashBucket *bn = hash->buckets[h].next;
                hash->buckets[h].key = bn->key;
                hash->buckets[h].value = bn->value;
                hash->buckets[h].next = bn->next;
                free(bn);
            } else {
                // this bucket becomes empty
                hash->buckets[h].key = NULL;
                hash->buckets[h].value = NULL;
            }
            hash->elemcount--;
            return 1;
        } else {
            // the item is not the first, walk the chain
            struct HashBucket *b = hash->buckets + h;
            while (b->next) {
                if (strcmp(b->next->key, key) == 0) {
                    // remove b->next from the chain
                    struct HashBucket *bn = b->next;
                    hash->item_delete_cb(bn->key, bn->value, hash->userdata);
                    b->next = bn->next;
                    free(bn);
                    hash->elemcount--;
                    return 1;
                }
                b = b->next;
            }
        }
    } else {
        // key not in the hash
    }
    return 0;
}

void *hashmap_find(const struct HashMap *hash, const char *key)
{
    if (!hash) return NULL;
    if (!key) return NULL;

    unsigned h = djb2_hash(key) % hash->bucketcount;

    if (hash->buckets[h].key) {
        for (struct HashBucket *b=hash->buckets+h; b; b=b->next) {
            if (strcmp(b->key, key) == 0) {
                return b->value;
            }
        }
    }

    return NULL;
}

int hashmap_contains(const struct HashMap *hash, const char *key)
{
    if (!hash) return 0;
    if (!key) return 0;

    unsigned h = djb2_hash(key) % hash->bucketcount;

    if (hash->buckets[h].key) {
        for (struct HashBucket *b=hash->buckets+h; b; b=b->next) {
            if (strcmp(b->key, key) == 0) {
                return 1;
            }
        }
    }

    return 0;
}

unsigned hashmap_count(const struct HashMap *hash)
{
    if (!hash) return 0;
    return hash->elemcount;
}

unsigned hashmap_bucketcount(const struct HashMap *hash)
{
    if (!hash) return 0;
    return hash->bucketcount;
}

unsigned hashmap_usedbuckets(const struct HashMap *hash)
{
    if (!hash) return 0;
    unsigned count = 0;
    for (unsigned i=0; i<hash->bucketcount; i++) {
        if (hash->buckets[i].key) {
            count ++;
        }
    }
    return count;
}

int hashmap_foreach(const struct HashMap *hash, hashmap_cb *cb, void *userdata)
{
    if (!hash) return 0;
    if (!cb) return 0;

    for (unsigned i=0; i<hash->bucketcount; i++) {
        if (hash->buckets[i].key) {
            struct HashBucket *b = hash->buckets+i;
            const char *bkey;
            // if @cb removes the item, we must re-run with the new item in the bucket
            do {
                bkey = b->key;
                if (!cb(b->key, b->value, userdata)) return 0;
            } while (b->key && (b->key != bkey));

            if (b->key) {
                // bucket is not empty, process the chain if there is any
                struct HashBucket *bb = b->next;
                while (bb) {
                    // @cb can remove bb from the chain
                    struct HashBucket *bn = bb->next;
                    if (!cb(bb->key, bb->value, userdata)) return 0;
                    bb = bn;
                }
            }
        }
    }
    return 1;
}

static int hash_key_cmp(const void *p1, const void *p2)
{
    const struct HashBucket *const*b1 = (const struct HashBucket *const*)p1;
    const struct HashBucket *const*b2 = (const struct HashBucket *const*)p2;
    return strcmp((*b1)->key, (*b2)->key);
}

int hashmap_foreach_sorted(const struct HashMap *hash, hashmap_cb *cb, void *userdata)
{
    if (!hash) return 0;
    if (!cb) return 0;

    struct HashBucket **buckets = (struct HashBucket **)malloc(hash->elemcount*sizeof(struct HashBucket *));
    unsigned j = 0;
    for (unsigned i=0; i<hash->bucketcount; i++) {
        if (hash->buckets[i].key) {
            for (struct HashBucket *b=hash->buckets+i; b; b=b->next) {
                buckets[j++] = b;
            }
        }
    }
    qsort(buckets, hash->elemcount, sizeof(struct HashBucket *), hash_key_cmp);
    for (unsigned i=0; i<hash->elemcount; i++) {
        if (!cb(buckets[i]->key, buckets[i]->value, userdata)) {
            free(buckets);
            return 0;
        }
    }
    free(buckets);
    return 1;
}

static int rehash_item_remove_cb(const char *key, void *value, void *userdata)
{
    (void)key;
    (void)value;
    (void)userdata;
    return 1;
}

static int rehash_item_move_cb(const char *key, void *value, void *userdata)
{
    struct HashMap *newhash = (struct HashMap *)userdata;
    hashmap_insert(newhash, (char*)key, value);
    return 1;
}

void hashmap_rehash(struct HashMap *hash, unsigned bucketcount)
{
    if (!hash) return;
    if (bucketcount == 0) return;
    if (bucketcount == hash->bucketcount) return;

    struct HashMap *newhash = new_hashmap(bucketcount, rehash_item_remove_cb, NULL);

    // link the contents into new hash
    hashmap_foreach(hash, rehash_item_move_cb, newhash);

    // swap the contents
    struct HashBucket *tmpb = newhash->buckets;
    newhash->buckets = hash->buckets;
    hash->buckets = tmpb;
    newhash->bucketcount = hash->bucketcount;
    hash->bucketcount = bucketcount;

    // this doesn't delete items
    delete_hashmap(newhash);
}

struct HashMapIterator hash_iterator(const struct HashMap *hash)
{
    struct HashMapIterator iter = { .hash_ = hash, .i_ = 0, .b_ = NULL };
    unsigned i = 0;
    struct HashBucket *b = hash->buckets;
    // skip until we find a bucket that is not empty
    while (i < hash->bucketcount && b->key == NULL) {
        i++;
        b = hash->buckets + i;
    }
    iter.i_ = i;
    iter.b_ = b;
    return iter;
}

const char *hash_iterator_key(const struct HashMapIterator *iter)
{
    if (!hash_iterator_valid(iter)) return NULL;
    struct HashBucket *b = (struct HashBucket *)iter->b_;
    return b->key;
}

void *hash_iterator_value(const struct HashMapIterator *iter)
{
    if (!hash_iterator_valid(iter)) return NULL;
    struct HashBucket *b = (struct HashBucket *)iter->b_;
    return b->value;
}

int hash_iterator_valid(const struct HashMapIterator *iter)
{
    return iter->i_ < iter->hash_->bucketcount;
}

void hash_iterator_next(struct HashMapIterator *iter)
{
    if (!hash_iterator_valid(iter)) return;
    if (((struct HashBucket *)iter->b_)->next) {
        iter->b_ = ((struct HashBucket *)iter->b_)->next;
    } else {
        // step to next occupied bucket
        do {
            iter->i_++;
        } while ((iter->i_ < iter->hash_->bucketcount)
                && (iter->hash_->buckets[iter->i_].key == NULL));
        iter->b_ = iter->hash_->buckets + iter->i_;
    }
}

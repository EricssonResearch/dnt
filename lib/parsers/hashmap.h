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

#ifndef HASHMAP_H
#define HASHMAP_H

/**
 * A simple string-keyed hash map (also called dictionary)
 *
 * It implements hashing into the specified number of buckets, and linear search
 * among the elements within the bucket. Runtime adjustment to the bucket count
 * is supported, but doesn't happen automatically.
 *
 * Donald Knuth advises that the bucket count should be a prime number. AFAIK
 * it's not proven to be optimal, but Knuth is always right.
 *
 * The bucket count should be slightly larger than the expected number of items
 * for optimal performance. Hash collisions (multiple keys having the same hash
 * value) are resolved by chaining inside the buckets.
 *
 * The data pointed by the key and value pointers supplied to hashmap_insert()
 * are not copied, just the given pointers are remembered. The @item_delete_cb
 * callback is called whenever an element is removed, so the user can take care
 * of the disposal of the dynamic data. This callback is optional, the default
 * one calls free() for both the key and the value.
 *
 * NULL values are allowed, NULL keys are not.
 *
 * There are two interfaces for iterating the hash:
 *  - hashmap_foreach and hashmap_foreach_sorted
 *  - HashMapIterator
 * It is only safe to modify the hash during hashmap_foreach, not the others.
 */

#ifdef __cplusplus
extern "C" {
#endif

struct HashMap;

// callback for foreach and delete
// the key is const because that should never be changed in foreach
typedef int hashmap_cb(const char *key, void *value, void *userdata);

// create a hash map with @bucketcount buckets
// the callback will be called when an item gets removed from the hash
// the callback is optional, the builtin one calls free() for the key and the value
struct HashMap *new_hashmap(unsigned bucketcount, hashmap_cb *item_delete_cb, void *userdata);

// deletes the entire hash map, including all the stored items
// calls the delete callback for each item in the hashmap
// always @returns NULL
struct HashMap *delete_hashmap(struct HashMap *hash);

// add (key, value) pair to the hash map
// if this key already exists in the hash this overrides that value
// when an existing value is overridden, the delete callback is called with the old key and value
// @returns 1 if a new item was inserted into the hash
// @returns 0 on error or an existing item was overwritten
int hashmap_insert(struct HashMap *hash, char *key, void *value);

// removes the (key, value) pair from the hash map, if it exists
// the delete callback is called for the element
// @returns 1 if an item was deleted, 0 otherwise
int hashmap_remove(struct HashMap *hash, const char *key);

// @returns the value associated with the given key
// @returns NULL if the key is not in the hash or the value is NULL
void *hashmap_find(const struct HashMap *hash, const char *key);

// @returns true if the given key exists in the hash
// doesn't touch the associated value
int hashmap_contains(const struct HashMap *hash, const char *key);

// @returns the number of entries in the hash
// runtime: zero
unsigned hashmap_count(const struct HashMap *hash);

// @returns the number of buckets in the hash
// runtime: zero
unsigned hashmap_bucketcount(const struct HashMap *hash);

// @returns the number of buckets that contain value
// runtime: linear
unsigned hashmap_usedbuckets(const struct HashMap *hash);

// calls @cb for each element in the hash
// the elements are not ordered by their keys
// @userdata is passed to the callback
// stops and returns false if the callback returns false
// @returns true on success
// it is safe to call @hashmap_remove on the current item in @cb (only on that one!)
int hashmap_foreach(const struct HashMap *hash, hashmap_cb *cb, void *userdata);

// calls @cb for each element in the hash
// the elements are ordered by their keys
// @userdata is passed to the callback
// stops and returns false if the callback returns false
// it is not safe to call @hashmap_remove in @cb
int hashmap_foreach_sorted(const struct HashMap *hash, hashmap_cb *cb, void *userdata);

// rearranges the contents to have @bucketcount buckets
// the items in @hash are untouched, only the buckets are changed
void hashmap_rehash(struct HashMap *hash, unsigned bucketcount);

// the contents of this structure are all private
struct HashMapIterator {
    const struct HashMap *hash_;
    unsigned i_;
    void *b_;
};

// creates a new iterator
// it is not safe to modify the hash while iterating it
struct HashMapIterator hash_iterator(const struct HashMap *hash);

// @returns the key of the current item
// @returns NULL if the iterator is not valid
const char *hash_iterator_key(const struct HashMapIterator *iter);

// @returns the value of the current item
// @returns NULL if the iterator is not valid
void *hash_iterator_value(const struct HashMapIterator *iter);

// @returns true if the iterator points to a valid item
// use this to terminate the iteration
int hash_iterator_valid(const struct HashMapIterator *iter);

// steps the iterator to the next item in the hash
// doesn't do anything if the iterator is not valid
void hash_iterator_next(struct HashMapIterator *iter);

#define HASHMAP_ITERATE(hash_, iter_)                               \
    for (struct HashMapIterator iter_ = hash_iterator(hash_);       \
            hash_iterator_valid(&iter_); hash_iterator_next(&iter_))

#ifdef __cplusplus
}
#endif

#endif // HASHMAP_H

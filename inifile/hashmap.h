/*
 * Copyright (c) 2021 Miklós Máté
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
 * is not supported.
 *
 * Donald Knuth advises that the bucket count should be a prime number. AFAIK
 * it's not proven to be optimal, but Knuth is always right.
 *
 * The bucket count should be slightly larger than the expected number of items.
 *
 * The data pointed by the key and value pointers supplied to hashmap_insert()
 * are not copied, just the given pointers are remembered. The @item_delete_cb
 * callback is called whenever an element is removed, so the user can take care
 * of the disposal of the dynamic data. This callback is optional, the default
 * callback calls free() for both the key and the value.
 *
 * NULL values are allowed, NULL keys are not.
 */

struct HashMap;

typedef int hashmap_cb(const char *key, void *value, void *userdata);

// create a hash map with @bucketcount buckets
// the callback will be called when an element gets removed from the hash
// the callback is optional, the builtin one calls free() for the key and the value
struct HashMap *new_hashmap(unsigned bucketcount, hashmap_cb *item_delete_cb, void *userdata);

// deletes the entire hash map, including all the stored elements
// calls the callback for each removed element
// always returns NULL
struct HashMap *delete_hashmap(struct HashMap *hash);

// add (key, value) pair to the hash map
// if this key already exists in the hash this overrides that value
// when an existing value is overridden, the delete callback is called with the old key and value
void hashmap_insert(struct HashMap *hash, char *key, void *value);

// removes the (key, value) pair from the hash map, if it exists
// the delete callback is called for the element
void hashmap_remove(struct HashMap *hash, const char *key);

// returns the value associated with the given key
// returns NULL if the key is not in the hash or the value is NULL
void *hashmap_find(struct HashMap *hash, const char *key);

// returns true if the given key exists in the hash even if the associated
// value is NULL
int hashmap_contains(const struct HashMap *hash, const char *key);

// returns the number of entries in the hash
unsigned hashmap_count(const struct HashMap *hash);

// returns the number of buckets in the hash
unsigned hashmap_bucketcount(const struct HashMap *hash);

// return the number of buckets that contain value
unsigned hashmap_usedbuckets(const struct HashMap *hash);

// calls @cb for each element in the hash
// the elements are not ordered by their keys
// @userdata is passed to the callback
// stops and returns false if the callback returns false
// returns true on success
//TODO is it safe to remove stuff in foreach?
int hashmap_foreach(const struct HashMap *hash, hashmap_cb *cb, void *userdata);

// calls @cb for each element in the hash
// the elements are ordered by their keys
// @userdata is passed to the callback
// stops and returns false if the callback returns false
int hashmap_foreach_sorted(const struct HashMap *hash, hashmap_cb *cb, void *userdata);

#endif // HASHMAP_H

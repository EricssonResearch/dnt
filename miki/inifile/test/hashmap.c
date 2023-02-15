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

#include "testing.h"

#include "hashmap.h"
#include "iniutils.h"

#include <stdlib.h>
#include <string.h>

TEST_INIT("Hash Map");

static void nofree_cb(const char *key, void *value, void *userdata)
{
    (void)key;
    (void)value;
    (void)userdata;
}

static void abcverify_cb(const char *key, void *value, void *userdata)
{
    int *abcverify = userdata;
    OK(key == value, "key==value");
    int v = *key - 'a';
    *abcverify |= 1<<v;
    //printf("key '%s' value '%s'\n", key, (char*)value);
}

static void abcverify_order_cb(const char *key, void *value, void *userdata)
{
    int *abcverify = userdata;
    OK(key == value, "key==value");
    int v = *key - 'a';
    OK(v > *abcverify, "next char");
    *abcverify = v;
    //printf("key '%s' value '%s'\n", key, (char*)value);
}

static void test_insert(void)
{
    const unsigned lettercount = 26;
    const unsigned bucketcount = 5;
    // no free because we'll insert pointers into a character array
    struct HashMap *hash = new_hashmap(bucketcount, nofree_cb, NULL);
    OK_FATAL(hash != NULL, "create hash");

    // first let's see an empty hash
    OK(hashmap_count(hash) == 0, "count");
    OK(hashmap_bucketcount(hash) == bucketcount, "bucket count");
    OK(hashmap_usedbuckets(hash) == 0, "used buckets");
    OK(hashmap_find(hash, "some key") == NULL, "find in empty");
    OK(hashmap_contains(hash, "some key") == 0, "empty doesn't contain");

    char abc[lettercount+1];
    for (unsigned i=0; i<lettercount; i++) {
        abc[i] = 'a' + i;
    }
    abc[lettercount] = 0;
    for (unsigned i=0; i<lettercount; i++) {
        hashmap_insert(hash, abc+i, abc+i);
        //printf("insert '%s'\n", abc+i);
    }
    OK(hashmap_bucketcount(hash) == bucketcount, "bucketcount");
    OK(hashmap_count(hash) == lettercount, "elemcount");
    OK(hashmap_usedbuckets(hash) == bucketcount, "all buckets should be in use");
    int abcverify = 0;
    hashmap_foreach(hash, abcverify_cb, &abcverify);
    OK(abcverify == 0x3ffffff, "abcverify 0x%x", abcverify);

    abcverify = -1;
    hashmap_foreach_sorted(hash, abcverify_order_cb, &abcverify);
    OK(abcverify == 25, "elem count %d %d", abcverify, lettercount);

    // these literals have different address than the keys
    OK(hashmap_contains(hash, "qrstuvwxyz"), "contains");
    OK(hashmap_contains(hash, "xyz"), "contains");
    OK(hashmap_contains(hash, "zyx") == 0, "not contains");
    OK(hashmap_find(hash, "fghijklmnopqrstuvwxyz") == abc+5, "find");
    OK(hashmap_find(hash, "fghijklmnopqrstuvwxyza") == NULL, "not find");

    delete_hashmap(hash);
}

static void test_change(void)
{
    const unsigned bucketcount = 11;
    // use the default item delete callback
    struct HashMap *hash = new_hashmap(bucketcount, NULL, NULL);
    OK_FATAL(hash != NULL, "create hash");

    hashmap_insert(hash, u_strdup("key"), u_strdup("value"));
    OK(hashmap_usedbuckets(hash) == 1, "one bucket");
    hashmap_insert(hash, u_strdup("another key"), u_strdup("value"));
    OK(hashmap_usedbuckets(hash) == 2, "two buckets"); // or the hash function is garbage
    OK(strcmp(hashmap_find(hash, "key"), "value") == 0, "find");
    hashmap_insert(hash, u_strdup("key"), u_strdup("some other value"));
    OK(hashmap_usedbuckets(hash) == 2, "two buckets");
    OK(strcmp(hashmap_find(hash, "key"), "some other value") == 0, "find");
    OK(strcmp(hashmap_find(hash, "another key"), "value") == 0, "find");

    // overwrite value in the hash without inserting with the same key
    float *f = malloc(sizeof(float));
    *f = 3.14;
    hashmap_insert(hash, u_strdup("float"), f);
    float *hf = hashmap_find(hash, "float");
    OK(hf, "found float");
    *hf = 0.5; // float can represent this value exactly
    OK(*f == 0.5, "value overwrite %f", *f);

    hashmap_remove(hash, "float");
    hashmap_remove(hash, "key");
    OK(hashmap_usedbuckets(hash) == 1, "one bucket"); // we know hash contains 1 item now
    hashmap_remove(hash, "another key");
    OK(hashmap_count(hash) == 0, "empty");
    delete_hashmap(hash);

    // massive add-remove test
    unsigned m = 20;
    unsigned n = 200;
    hash = new_hashmap(bucketcount, NULL, NULL);
    char buf[16];
    for (unsigned i=0; i<m; i++) {
        for (unsigned j=0; j<n; j++) {
            sprintf(buf, "hash %u %u", i, j);
            hashmap_insert(hash, u_strdup(buf), u_strdup(buf));
        }
        OK(hashmap_count(hash) == n+i, "full %u", hashmap_count(hash));
        // remove all but one
        if (i % 2) {
            for (unsigned j=0; j<n-1; j++) {
                sprintf(buf, "hash %d %d", i, j);
                hashmap_remove(hash, buf);
            }
        } else {
            for (unsigned j=n-1; j>0; j--) {
                sprintf(buf, "hash %d %d", i, j);
                hashmap_remove(hash, buf);
            }
        }
        OK(hashmap_count(hash) == i+1, "almost empty %u", hashmap_count(hash));
    }
    delete_hashmap(hash);
}


TEST_CASES = {
    {"insert", test_insert},
    {"change", test_change},
    {NULL, NULL}
};


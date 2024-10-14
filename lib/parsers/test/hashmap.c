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
#include "parserutils.h"

#include <stdlib.h>
#include <string.h>

TEST_INIT("Hash Map");

static int nofree_cb(const char *key, void *value, void *userdata)
{
    (void)key;
    (void)value;
    (void)userdata;
    return 1;
}

static int abcverify_cb(const char *key, void *value, void *userdata)
{
    int *abcverify = (int*)userdata;
    OK(key == value, "key==value");
    int v = *key - 'a';
    *abcverify |= 1<<v;
    //printf("key '%s' value '%s'\n", key, (char*)value);
    return 1;
}

static int abcverify_order_cb(const char *key, void *value, void *userdata)
{
    int *abcverify = (int*)userdata;
    OK(key == value, "key==value");
    int v = *key - 'a';
    OK(v > *abcverify, "next char");
    *abcverify = v;
    //printf("key '%s' value '%s'\n", key, (char*)value);
    return 1;
}

static int abcverify_interrupt_cb(const char *key, void *value, void *userdata)
{
    int *abcverify = (int*)userdata;
    OK(key == value, "key==value");
    int v = *key - 'a';
    *abcverify |= 1<<v;
    if (v == 10)
        return 0;
    else
        return 1;
}

static int abcverify_order_interrupt_cb(const char *key, void *value, void *userdata)
{
    int *abcverify = (int*)userdata;
    OK(key == value, "key==value");
    int v = *key - 'a';
    OK(v > *abcverify, "next char");
    *abcverify = v;
    if (v == 10)
        return 0;
    else
        return 1;
}

static void test_insert(void)
{
    enum { lettercount = 26 }; // the true constant expression in C
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

    // test foreach
    char abc[lettercount+1];
    for (unsigned i=0; i<lettercount; i++) {
        abc[i] = 'a' + i;
    }
    abc[lettercount] = 0;
    for (unsigned i=0; i<lettercount; i++) {
        OK(hashmap_insert(hash, abc+i, abc+i) == 1, "new item");
        //printf("insert '%s'\n", abc+i);
    }
    OK(hashmap_bucketcount(hash) == bucketcount, "bucketcount");
    OK(hashmap_count(hash) == lettercount, "elemcount");
    OK(hashmap_usedbuckets(hash) == bucketcount, "all buckets should be in use");
    int abcverify = 0;
    OK(hashmap_foreach(hash, abcverify_cb, &abcverify) == 1, "foreach successful");
    OK(abcverify == 0x3ffffff, "abcverify 0x%x", abcverify);

    abcverify = -1;
    OK(hashmap_foreach_sorted(hash, abcverify_order_cb, &abcverify) == 1, "foreach successful");
    OK(abcverify == lettercount-1, "elem count %d %d", abcverify, lettercount);

    OK(hashmap_foreach(NULL, NULL, NULL) == 0, "foreach null");
    OK(hashmap_foreach(hash, NULL, NULL) == 0, "foreach null");
    OK(hashmap_foreach(NULL, abcverify_cb, NULL) == 0, "foreach null");
    OK(hashmap_foreach_sorted(NULL, NULL, NULL) == 0, "foreach null");
    OK(hashmap_foreach_sorted(hash, NULL, NULL) == 0, "foreach null");
    OK(hashmap_foreach_sorted(NULL, abcverify_order_cb, NULL) == 0, "foreach null");

    // test interrupting foreach
    abcverify = 0;
    hashmap_foreach(hash, abcverify_interrupt_cb, &abcverify);
    OK(abcverify < 0x3ffffff, "abcverify 0x%x", abcverify);

    abcverify = -1;
    hashmap_foreach_sorted(hash, abcverify_order_interrupt_cb, &abcverify);
    OK(abcverify == 10, "elem count %d %d", abcverify, lettercount);

    // these literals have different address than the keys
    OK(hashmap_contains(hash, "qrstuvwxyz"), "contains");
    OK(hashmap_contains(hash, "xyz"), "contains");
    OK(hashmap_contains(hash, "zyx") == 0, "not contains");
    OK(hashmap_find(hash, "fghijklmnopqrstuvwxyz") == abc+5, "find");
    OK(hashmap_find(hash, "fghijklmnopqrstuvwxyza") == NULL, "not find");

    OK(hashmap_insert(hash, abc+3, abc+4) == 0, "overwrite existing");
    OK(hashmap_find(hash, abc+3) == abc+4, "overwrite successful");
    OK(hashmap_insert(hash, NULL, abc+5) == 0, "null key not allowed");
    OK(hashmap_insert(NULL, abc+6, abc+7) == 0, "null hash");

    OK(delete_hashmap(hash) == NULL, "delete");
    OK(delete_hashmap(NULL) == NULL, "delete null");

    OK(new_hashmap(0, NULL, NULL) == NULL, "no buckets");
}

struct Remove {
    struct HashMap *hash;
    int divisor;
};
static int remove_divisible(const char *key, void *value, void *userdata)
{
    (void)value;
    struct Remove *rem = (struct Remove *)userdata;
    int num;
    if (sscanf(key, "hash %d", &num) != 1) return 0;
    if (num % rem->divisor == 0)
        hashmap_remove(rem->hash, key);
    return 1;
}

static void test_change(void)
{
    const unsigned bucketcount = 11;
    // use the default item delete callback
    struct HashMap *hash = new_hashmap(bucketcount, NULL, NULL);
    OK_FATAL(hash != NULL, "create hash");

    OK(hashmap_insert(hash, u_strdup("key"), u_strdup("value")) == 1, "new item");
    OK(hashmap_usedbuckets(hash) == 1, "one bucket");
    OK(hashmap_insert(hash, u_strdup("another key"), u_strdup("value")) == 1, "new item");
    OK(hashmap_usedbuckets(hash) == 2, "two buckets"); // or the hash function is garbage
    OK(strcmp((char*)hashmap_find(hash, "key"), "value") == 0, "find");
    OK(hashmap_insert(hash, u_strdup("key"), u_strdup("some other value")) == 0, "overwrite");
    OK(hashmap_usedbuckets(hash) == 2, "two buckets");
    OK(strcmp((char*)hashmap_find(hash, "key"), "some other value") == 0, "find overwritten value");
    OK(strcmp((char*)hashmap_find(hash, "another key"), "value") == 0, "find unchanged value");

    // overwrite value in the hash without inserting with the same key
    float *f = (float*)malloc(sizeof(float));
    *f = 3.14;
    OK(hashmap_insert(hash, u_strdup("float"), f) == 1, "new item");
    float *hf = (float*)hashmap_find(hash, "float");
    OK(hf, "found float");
    *hf = 0.5; // float can represent this value exactly
    OK(*f == 0.5, "value overwrite %f", *f);

    OK(hashmap_remove(hash, NULL) == 0, "no key");
    OK(hashmap_remove(NULL, "null") == 0, "no hash");
    OK(hashmap_remove(hash, "float") == 1, "removed");
    OK(hashmap_remove(hash, "key") == 1, "removed");
    OK(hashmap_usedbuckets(hash) == 1, "one bucket"); // we know hash contains 1 item now
    OK(hashmap_remove(hash, "another key") == 1, "removed");
    OK(hashmap_count(hash) == 0, "empty");
    OK(delete_hashmap(hash) == NULL, "delete");

    // massive add-remove test
    const unsigned m = 20;
    const unsigned n = 300;
    char buf[16];
    hash = new_hashmap(bucketcount, NULL, NULL);
    OK_FATAL(hash != NULL, "create hash");
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
    OK(delete_hashmap(hash) == NULL, "delete");

    // remove in foreach
    hash = new_hashmap(bucketcount, NULL, NULL);
    OK_FATAL(hash != NULL, "create hash");
    for (unsigned j=0; j<n; j++) {
        sprintf(buf, "hash %u", j);
        hashmap_insert(hash, u_strdup(buf), u_strdup(buf));
    }
    OK(hashmap_count(hash) == n, "full %u", hashmap_count(hash));
    struct Remove rem = {hash, 2};
    OK(hashmap_foreach(hash, remove_divisible, &rem) == 1, "successful remove");
    OK(hashmap_count(hash) == n/2, "half %u", hashmap_count(hash));
    rem.divisor = 1;
    OK(hashmap_foreach(hash, remove_divisible, &rem) == 1, "successful remove");
    OK(hashmap_count(hash) == 0, "empty %u", hashmap_count(hash));
    OK(delete_hashmap(hash) == NULL, "delete");
}

static void test_rehash(void)
{
    const unsigned m = 20;
    const unsigned n = 100;
    char buf[16];
    struct HashMap *hash = new_hashmap(23, NULL, NULL);
    for (unsigned i=0; i<m; i++) {
        for (unsigned j=0; j<n; j++) {
            sprintf(buf, "hash %u %u", i, j);
            hashmap_insert(hash, u_strdup(buf), u_strdup(buf));
        }
    }
    OK(hashmap_count(hash) == m*n, "count %u", hashmap_count(hash));
    hashmap_rehash(hash, 13);
    OK(hashmap_bucketcount(hash) == 13, "bucketcount %u", hashmap_bucketcount(hash));
    OK(hashmap_count(hash) == m*n, "count %u", hashmap_count(hash));
    for (unsigned i=0; i<m; i++) {
        for (unsigned j=0; j<n; j++) {
            sprintf(buf, "hash %u %u", i, j);
            char *val = (char*)hashmap_find(hash, buf);
            OK(val != NULL, "missing '%s'", buf);
            OK(strcmp(val, buf) == 0, "key '%s' value '%s'", buf, val);
        }
    }
    hashmap_rehash(hash, 31);
    OK(hashmap_bucketcount(hash) == 31, "bucketcount %u", hashmap_bucketcount(hash));
    OK(hashmap_count(hash) == m*n, "count %u", hashmap_count(hash));
    for (unsigned i=0; i<m; i++) {
        for (unsigned j=0; j<n; j++) {
            sprintf(buf, "hash %u %u", i, j);
            char *val = (char*)hashmap_find(hash, buf);
            OK(val != NULL, "missing '%s'", buf);
            OK(strcmp(val, buf) == 0, "key '%s' value '%s'", buf, val);
        }
    }
    hashmap_rehash(hash, 31);
    OK(hashmap_bucketcount(hash) == 31, "bucketcount %u", hashmap_bucketcount(hash));
    OK(hashmap_count(hash) == m*n, "count %u", hashmap_count(hash));
    OK(delete_hashmap(hash) == NULL, "delete");
}


TEST_CASES = {
    {"insert", test_insert},
    {"change", test_change},
    {"rehash", test_rehash},
    {NULL, NULL}
};


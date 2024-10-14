
#include "testing.h"

#include "utils.h"
#include "hashmap.h"

#include <stdlib.h>
#include <string.h>

TEST_INIT("Utils");

static void test_divceil(void)
{
    for (unsigned y=1; y<32; y++) {
        for (unsigned x=0; x<32; x++) {
            unsigned expected = 0;
            unsigned tmp = x;
            while (tmp >=y) { expected++; tmp -= y; }
            if (tmp) expected++;

            unsigned z = DIVCEIL(x, y);

            OK(z == expected, "x %u y %u z %u expected %u", x, y, z, expected);
        }
    }

    OK(DIVCEIL(8+14, 19-14) == 5, "argument parentheses?");
}

static void test_reverse_list(void)
{
    struct L {unsigned u; struct L *next; };

    struct L *list = NULL;

    for (unsigned i=0; i<10; i++) {
        struct L *n = calloc_struct(L);
        n->u = i;
        n->next = list;
        list = n;
    }
    REVERSE_LIST(list);

    unsigned count = 0;
    for (struct L *l=list; l; l=l->next) {
        OK(l->u == count, "count %u u %u", count, l->u);
        count++;
    }
    OK(count == 10, "count %u", count);

    while (list) {
        struct L *d = list;
        list = list->next;
        free(d);
    }
}

static void test_memdup(void)
{
    unsigned onstack[10];
    for (unsigned i=0; i<10; i++) onstack[i] = i;
    unsigned *dup = (unsigned*)memdup(onstack, 10*sizeof(unsigned));
    OK_FATAL(dup != NULL, "have dup");
    OK(dup != onstack, "new allocation");
    for (unsigned i=0; i<10; i++) {
        OK(dup[i] == onstack[i], "dup %u onstack %u", dup[i], onstack[i]);
    }
    free(dup);

    unsigned *onheap = (unsigned*)malloc(10*sizeof(unsigned));
    for (unsigned i=0; i<10; i++) onheap[i] = i;
    dup = (unsigned*)memdup(onheap, 10*sizeof(unsigned));
    OK_FATAL(dup != NULL, "have dup");
    OK(dup != onheap, "new allocation");
    for (unsigned i=0; i<10; i++) {
        OK(dup[i] == onheap[i], "dup %u onheap %u", dup[i], onheap[i]);
    }
    free(dup);
    free(onheap);
}

static void test_strdup_printf(void)
{
    const char *pattern = "dupping %u %s";
    char *dup1 = strdup_printf(pattern, 424, "somethings");
    OK_FATAL(dup1 != NULL, "have dup");
    OK(dup1 != pattern, "new allocation");
    OK(strcmp(dup1, "dupping 424 somethings") == 0, "string '%s'", dup1);

    char *dup2 = strdup_printf(pattern, 3, dup1);
    OK_FATAL(dup2 != NULL, "have dup");
    OK(dup2 != pattern, "new allocation");
    OK(dup2 != dup1, "new allocation");
    OK(strcmp(dup2, "dupping 3 dupping 424 somethings") == 0, "string '%s'", dup2);

    free(dup1);
    free(dup2);

    char *unicode = strdup_printf("árvíztűrő %s", "tükörfúrógép");
    OK_FATAL(unicode != NULL, "have string");
    OK(strcmp(unicode, "árvíztűrő tükörfúrógép") == 0, "got '%s'", unicode);
    free(unicode);
}

static void test_hashmap_foreach_nocb(void)
{
    struct HashMap *h = new_hashmap(11, NULL, NULL);
    OK_FATAL(h != NULL, "have hash");
    hashmap_insert(h, strdup("key1"), strdup("val1"));
    hashmap_insert(h, strdup("key2"), strdup("val2"));
    hashmap_insert(h, strdup("key3"), strdup("val3"));

    unsigned count = 0;
    hashmap_foreach_nocb(h, char) {
        count++;
        OK_FATAL(key != NULL, "have key");
        OK_FATAL(value != NULL, "have value");
        unsigned n;
        char err;
        OK(sscanf(key, "key%u%c", &n, &err) == 1, "key string '%s'", key);
        char *val = strdup_printf("val%u", n);
        OK(strcmp(value, val) == 0, "value string '%s'", value);
        free(val);
    }
    OK(count == 3, "count %u", count);
    delete_hashmap(h);

    // test with no value
    h = new_hashmap(11, NULL, NULL);
    OK_FATAL(h != NULL, "have hash");
    hashmap_insert(h, strdup("key1"), NULL);
    hashmap_insert(h, strdup("key2"), NULL);
    hashmap_insert(h, strdup("key3"), NULL);

    count = 0;
    hashmap_foreach_nocb(h, char) {
        OK(key != NULL, "no key");
        OK(value == NULL, "value");
        count++;
    }
    OK(count == 3, "count %u", count);
    delete_hashmap(h);
}

TEST_CASES = {
    {"divceil", test_divceil},
    {"reverse list", test_reverse_list},
    {"memdup", test_memdup},
    {"strdup_printf", test_strdup_printf},
    {"hashmap_foreach_nocb", test_hashmap_foreach_nocb},
    {NULL, NULL}
};

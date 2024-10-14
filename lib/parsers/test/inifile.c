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

#include "inifile.h"
#include "parserutils.h"

#include <string.h>
#include <stdlib.h>

TEST_INIT("INI File");

struct KeyValue {
    const char *key;
    const char *value;
};

static int section_verify_cb(const char *key, void *value, void *userdata)
{
    char *val = (char *)value;
    struct KeyValue *good = (struct KeyValue *)userdata;

    while (good->key) {
        if (strcmp(good->key, key) == 0) {
            OK(strcmp(good->value, val) == 0, "for key '%s' expected value '%s' got value '%s'",
                    key, good->value, val);
            return 1;
        }
        good++;
    }
    FAIL("unexpected key '%s'", key);
    return 1;
}

static void test_read(void)
{
    struct IniSection *ini_empty = read_inifile(SAMPLES_DIR "/good_empty.ini");
    OK_FATAL(ini_empty != NULL, "empty file is valid");
    delete_inisection(ini_empty);
    delete_inisection(NULL);

    struct IniSection *ini_zero = read_inifile(SAMPLES_DIR "/good_zerolength.ini");
    OK_FATAL(ini_zero != NULL, "zero length file is valid");
    delete_inisection(ini_zero);

    struct IniSection *ini_nonewline = read_inifile(SAMPLES_DIR "/good_nonewline.ini");
    OK_FATAL(ini_nonewline != NULL, "no newline at the end is valid");
    OK(hashmap_count(ini_nonewline->contents) == 1, "item count");
    struct KeyValue nonewline_kv[] = {
        {"key", "val"},
        {NULL, NULL},
    };
    hashmap_foreach(ini_nonewline->contents, section_verify_cb, nonewline_kv);
    delete_inisection(ini_nonewline);

    struct IniSection *ini_nosection = read_inifile(SAMPLES_DIR "/good_nosection.ini");
    OK_FATAL(ini_nosection != NULL, "nosection file is valid");
    // check contents against known good contents
    OK(ini_nosection->name == NULL, "no section name");
    OK(hashmap_count(ini_nosection->contents) == 12, "item count");
    struct KeyValue nosection_kv[] = {
        {"key1", "value1"},
        {"key2", "value2"},
        //{"key3", "value33"}, //XXX now duplicate keys are error
        {"key3", "value3"},
        {"key4", "value4"},
        {"key5", "value5"},
        {"key6", "value6 has whitespace at the end     "},
        {"key7isMuchLongerBecauseWeWantToTestLongerKeysToo"
            "ButI'mNotEntirelySureHowLongThisShouldGoOnProbablyNowIsAGoodPlaceToStop", "value7"},
        {"key8", "Value8 is much longer than the previous ones, and unlike keys the values can contain whitespace too."
            " It can also contain any other symbol. The end of the value is at the end of the line;"
                " we currently don't support multiline values."},
        {"key9éáőú", "value9 because we need to test unicode too: árvíztűrő tükörfúrógép"},
        {"第十の鍵", "第十の値は撲の苦手な日本語"},
        {"key11", ""},
        {"key[name]", "value12"},
        {NULL, NULL},
    };
    hashmap_foreach(ini_nosection->contents, section_verify_cb, nosection_kv);
    OK(ini_nosection->next == NULL, "no next section");
    delete_inisection(ini_nosection);

    struct IniSection *ini_nostartsection = read_inifile(SAMPLES_DIR "/good_nostartsection.ini");
    OK_FATAL(ini_nostartsection != NULL, "nostartsection file is valid");
    // check contents against known good contents
    OK(ini_nostartsection->name == NULL, "first section is unnamed");
    OK(hashmap_count(ini_nostartsection->contents) == 2, "item count");
    struct KeyValue nostartsection1_kv[] = {
        {"key1", "value1"},
        {"key2", "value2"},
        {NULL, NULL},
    };
    hashmap_foreach(ini_nostartsection->contents, section_verify_cb, nostartsection1_kv);
    struct IniSection *ini_nostartsection_sec = ini_nostartsection->next;
    OK(ini_nostartsection_sec != NULL, "has a named section");
    OK(ini_nostartsection_sec->name != NULL && strcmp(ini_nostartsection_sec->name, "section1") == 0,
            "section name '%s'", ini_nostartsection_sec->name);
    OK(hashmap_count(ini_nostartsection_sec->contents) == 2, "item count");
    struct KeyValue nostartsection2_kv[] = {
        {"key3", "value3"},
        {"key4", "value4"},
        {NULL, NULL},
    };
    hashmap_foreach(ini_nostartsection_sec->contents, section_verify_cb, nostartsection2_kv);
    OK(ini_nostartsection_sec->next == NULL, "named section has no next");
    delete_inisection(ini_nostartsection);

    struct IniSection *ini_sections = read_inifile(SAMPLES_DIR "/good_sections.ini");
    OK_FATAL(ini_sections != NULL, "sections file is valid");
    // check contents against known good contents
    OK(ini_sections->name != NULL && strcmp(ini_sections->name, "section1") == 0, "section1 name '%s'", ini_sections->name);
    OK(hashmap_count(ini_sections->contents) == 1, "section1 item count");
    struct KeyValue sections1_kv[] = {
        {"key1", "value1"},
        {NULL, NULL},
    };
    hashmap_foreach(ini_sections->contents, section_verify_cb, sections1_kv);
    struct IniSection *ini_sections2 = ini_sections->next;
    OK(ini_sections2 != NULL, "have section 2");
    OK(ini_sections2->name != NULL && strcmp(ini_sections2->name, "section2") == 0, "section2 name '%s'", ini_sections2->name);
    OK(hashmap_count(ini_sections2->contents) == 1, "section2 item count");
    struct KeyValue sections2_kv[] = {
        {"key2", "value2"},
        {NULL, NULL},
    };
    hashmap_foreach(ini_sections2->contents, section_verify_cb, sections2_kv);
    struct IniSection *ini_sections3 = ini_sections2->next;
    OK(ini_sections3->name != NULL && strcmp(ini_sections3->name, "section 3") == 0, "section3 name '%s'", ini_sections3->name);
    OK(hashmap_count(ini_sections3->contents) == 1, "section3 item count");
    struct KeyValue sections3_kv[] = {
        {"key3", "value3"},
        {NULL, NULL},
    };
    hashmap_foreach(ini_sections3->contents, section_verify_cb, sections3_kv);
    struct IniSection *ini_sections4 = ini_sections3->next;
    OK(ini_sections4->name != NULL && strcmp(ini_sections4->name, "section name 4") == 0, "section4 name '%s'", ini_sections4->name);
    OK(hashmap_count(ini_sections4->contents) == 1, "section4 item count");
    struct KeyValue sections4_kv[] = {
        {"key4", "value4"},
        {NULL, NULL},
    };
    hashmap_foreach(ini_sections4->contents, section_verify_cb, sections4_kv);
    struct IniSection *ini_sections5 = ini_sections4->next;
    OK(ini_sections5->name != NULL && strcmp(ini_sections5->name, "unicode section name っては結構") == 0, "section5 name '%s'", ini_sections3->name);
    OK(hashmap_count(ini_sections5->contents) == 0, "section5 item count");
    struct IniSection *ini_sections6 = ini_sections5->next;
    OK(ini_sections6->name != NULL && strcmp(ini_sections6->name, "previous section was empty") == 0, "section6 name '%s'", ini_sections6->name);
    OK(hashmap_count(ini_sections6->contents) == 1, "section6 item count");
    struct KeyValue sections6_kv[] = {
        {"key6", "value 6"},
        {NULL, NULL},
    };
    hashmap_foreach(ini_sections6->contents, section_verify_cb, sections6_kv);
    struct IniSection *ini_sections7 = ini_sections6->next;
    OK(ini_sections7->name != NULL && strcmp(ini_sections7->name, "section name with [") == 0, "section7 name '%s'", ini_sections7->name);
    OK(hashmap_count(ini_sections7->contents) == 1, "section7 item count");
    struct KeyValue sections7_kv[] = {
        {"key7", "value 7"},
        {NULL, NULL},
    };
    hashmap_foreach(ini_sections7->contents, section_verify_cb, sections7_kv);
    OK(ini_sections7->next == NULL, "no more sections");

    struct IniSection *named = inisection_find_section(ini_sections, "section2");
    OK(named == ini_sections2, "find 2");
    named = inisection_find_section(ini_sections, "unicode section name っては結構");
    OK(named == ini_sections5, "find 5");
    named = inisection_find_section(ini_sections, "no such section");
    OK(named == NULL, "find none");
    named = inisection_find_section(ini_sections, NULL);
    OK(named == NULL, "find null");
    delete_inisection(ini_sections);

    struct IniSection *ini_longline = read_inifile(SAMPLES_DIR "/good_longline.ini");
    OK_FATAL(ini_longline != NULL, "longline file is valid");
    // check that we have one key,val pair in a single unnamed section
    OK(ini_longline->name == NULL, "unnamed");
    OK(hashmap_count(ini_longline->contents) == 2, "item count");
    OK(ini_longline->next == NULL, "no more sections");
    // check that the long value is correct
    const char *longvalue = inisection_get(ini_longline, "somewhatlongkeybutnotaslongastheotheroneinthisfile");
    OK(longvalue, "found longvalue");
    // the value is periodic
    const unsigned longperiod = 32;
    unsigned longlength = strlen(longvalue);
    for (unsigned i=0; i<longlength; i++) {
        if (i + longperiod < longlength) {
            if (longvalue[i] != longvalue[i+longperiod]) {
                FAIL("long value mismatch %u '%c' %u '%c'", i, longvalue[i], i+longperiod, longvalue[i+longperiod]);
            }
        }
    }
    // check long key
    char *longkey = u_strdup("verylongline");
    for (unsigned i=0; i<535; i++) {
        char *newlongkey = u_strcat(longkey, "verylongline");
        free(longkey);
        longkey = newlongkey;
    }
    const char *longkeyvalue = inisection_get(ini_longline, longkey);
    free(longkey);
    OK(longkeyvalue, "found longkey");
    OK(strcmp(longkeyvalue, "very long key") == 0, "longkey value");
    delete_inisection(ini_longline);

    struct IniSection *ini_dosendings = read_inifile(SAMPLES_DIR "/good_dosendings.ini");
    OK_FATAL(ini_dosendings != NULL, "dosendings file is valid");
    OK(ini_dosendings->name == NULL, "unnamed");
    OK(hashmap_count(ini_dosendings->contents) == 2, "item count");
    struct KeyValue doskeyval[] = {
        {"key1", "val1"},
        {"key2", "val2"},
    };
    hashmap_foreach(ini_dosendings->contents, section_verify_cb, doskeyval);
    OK(ini_dosendings->next == NULL, "no more sections");
    delete_inisection(ini_dosendings);
}

static void test_read_bad(void)
{
    struct IniSection *ini;
    ini = read_inifile(SAMPLES_DIR "/bad_keyspace.ini");
    OK(ini == NULL, "key has space");
    ini = read_inifile(SAMPLES_DIR "/bad_nokey.ini");
    OK(ini == NULL, "no key");
    ini = read_inifile(SAMPLES_DIR "/bad_novalue.ini");
    OK(ini == NULL, "no value");
    ini = read_inifile(SAMPLES_DIR "/bad_sectionclose.ini");
    OK(ini == NULL, "section header not cosed");
    ini = read_inifile(SAMPLES_DIR "/bad_sectionjunk.ini");
    OK(ini == NULL, "junk after section header");
    ini = read_inifile(SAMPLES_DIR "/bad_sectionname.ini");
    OK(ini == NULL, "section has no name");
    ini = read_inifile(SAMPLES_DIR "/bad_notexisting.ini");
    OK(ini == NULL, "file doesn't exist");
}

// returns 0 if identical
static int compare_files(const char *name1, const char *name2)
{
    FILE *f1 = fopen(name1, "r");
    FILE *f2 = fopen(name2, "r");

    int done = 0;
    do {
        int i1 = fgetc(f1);
        int i2 = fgetc(f2);
        if (i1 != i2) {
            fclose(f1);
            fclose(f2);
            return 1;
        }
        if (i1 == EOF) done = 1;
    } while (!done);

    fclose(f1);
    fclose(f2);
    return 0;
}

static void test_write(void)
{
    // write an ini that starts with some data then a section with some more data
    struct IniSection *ini = new_inisection(NULL);
    inisection_add(ini, "key1", "value1");
    inisection_add(ini, "key2", "value2");
    ini->next = new_inisection("a really good section");
    struct IniSection *sec = ini->next;
    inisection_add(sec, "key1", "value1");
    inisection_add(sec, "key2", "value2");

    /* The man page says tmpnam() should never be used, and the linker also issues a warning.
     * They say we should use tmpfile instead, but that's not good for us, because we want to
     * read back the file we created. I found no other portable (= in the C99 spec) way of
     * doing it.
     * BTW tmpnam() is dangerous from a security point of view, but here we don't really care
     * about security. Or thread safety.
     */
    char *outputname = tmpnam(NULL); // use the internal static buffer ;P
    //printf("output file name '%s'\n", outputname);
    int result = write_inifile(outputname, ini);
    OK(result == 0, "write successful");
    OK(compare_files(outputname, SAMPLES_DIR "/write_good1.ini") == 0, "output matches expectation");
    remove(outputname);
    delete_inisection(ini);

    // write an ini that has more sections
    ini = new_inisection("first section");
    inisection_add(ini, "key1", "value1");
    inisection_add(ini, "key2", "value2");
    ini->next = new_inisection("a second section");
    sec = ini->next;
    //inisection_add(sec, "key1", "value1");
    //inisection_add(sec, "key2", "value2");
    sec->next = new_inisection("wow third section");
    sec = sec->next;
    inisection_add(sec, "key3", "value3");
    inisection_add(sec, "key4", "value4");

    outputname = tmpnam(NULL);
    result = write_inifile(outputname, ini);
    OK(result == 0, "write successful");
    OK(compare_files(outputname, SAMPLES_DIR "/write_good2.ini") == 0, "output matches expectation");
    remove(outputname);
    delete_inisection(ini);
}

static void test_write_bad(void)
{
    // try to write an ini that has unnamed not-first section
    struct IniSection *ini = new_inisection("first section");
    inisection_add(ini, "key1", "value1");
    ini->next = new_inisection(NULL);
    struct IniSection *sec = ini->next;
    inisection_add(sec, "key1", "value1");

    char *outputname = tmpnam(NULL);
    int result = write_inifile(outputname, ini);
    OK(result != 0, "write error");
    remove(outputname);
    delete_inisection(ini);
}

//TODO multiple sections with the same name shouldn't be allowed

TEST_CASES = {
    {"read", test_read},
    {"read bad", test_read_bad},
    {"write", test_write},
    {"write bad", test_write_bad},
    {NULL, NULL}
};

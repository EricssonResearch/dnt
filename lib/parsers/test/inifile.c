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
    char *error;
    struct IniSection *ini_empty = read_inifile(SAMPLES_DIR "/good_empty.ini", &error);
    OK_FATAL(ini_empty != NULL, "empty file is valid");
    OK(error == NULL, "error '%s'", error);
    OK(delete_inisection(ini_empty) == NULL, "delete returns null");

    OK(delete_inisection(NULL) == NULL, "can delete null");

    struct IniSection *ini_zero = read_inifile(SAMPLES_DIR "/good_zerolength.ini", &error);
    OK_FATAL(ini_zero != NULL, "zero length file is valid");
    OK(delete_inisection(ini_zero) == NULL, "delete returns null");

    struct IniSection *ini_nonewline = read_inifile(SAMPLES_DIR "/good_nonewline.ini", &error);
    OK_FATAL(ini_nonewline != NULL, "no newline at the end is valid");
    OK(inisection_count(ini_nonewline) == 1, "item count %u", inisection_count(ini_nonewline));
    struct KeyValue nonewline_kv[] = {
        {"key", "val"},
        {NULL, NULL},
    };
    hashmap_foreach(ini_nonewline->contents, section_verify_cb, nonewline_kv);
    OK(delete_inisection(ini_nonewline) == NULL, "delete returns null");

    struct IniSection *ini_nosection = read_inifile(SAMPLES_DIR "/good_nosection.ini", &error);
    OK_FATAL(ini_nosection != NULL, "nosection file is valid");
    // check contents against known good contents
    OK(ini_nosection->name == NULL, "no section name");
    OK(inisection_sectioncount(ini_nosection) == 1, "one unnamed section");
    OK(inisection_count(ini_nosection) == 12, "item count %u", inisection_count(ini_nosection));
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
    OK(delete_inisection(ini_nosection) == NULL, "delete returns null");

    struct IniSection *ini_nostartsection = read_inifile(SAMPLES_DIR "/good_nostartsection.ini", &error);
    OK_FATAL(ini_nostartsection != NULL, "nostartsection file is valid");
    // check contents against known good contents
    OK(ini_nostartsection->name == NULL, "first section is unnamed");
    OK(inisection_sectioncount(ini_nostartsection) == 2, "one unnamed, one named");
    OK(inisection_count(ini_nostartsection) == 2, "item count %u", inisection_count(ini_nostartsection));
    struct KeyValue nostartsection1_kv[] = {
        {"key1", "value1"},
        {"key2", "value2"},
        {NULL, NULL},
    };
    hashmap_foreach(ini_nostartsection->contents, section_verify_cb, nostartsection1_kv);
    struct IniSection *ini_nostartsection_sec = ini_nostartsection->next;
    OK_FATAL(ini_nostartsection_sec != NULL, "has a named section");
    OK(ini_nostartsection_sec->name != NULL && strcmp(ini_nostartsection_sec->name, "section1") == 0,
            "section name '%s'", ini_nostartsection_sec->name);
    OK(inisection_count(ini_nostartsection_sec) == 2, "item count %u", inisection_count(ini_nostartsection_sec));
    struct KeyValue nostartsection2_kv[] = {
        {"key3", "value3"},
        {"key4", "value4"},
        {NULL, NULL},
    };
    hashmap_foreach(ini_nostartsection_sec->contents, section_verify_cb, nostartsection2_kv);
    OK(ini_nostartsection_sec->next == NULL, "named section has no next");
    OK(delete_inisection(ini_nostartsection) == NULL, "delete returns null");

    struct IniSection *ini_sections = read_inifile(SAMPLES_DIR "/good_sections.ini", &error);
    OK_FATAL(ini_sections != NULL, "sections file is valid");
    // check contents against known good contents
    OK(ini_sections->name != NULL && strcmp(ini_sections->name, "section1") == 0, "section1 name '%s'", ini_sections->name);
    OK(inisection_sectioncount(ini_sections) == 7, "sections count %u", inisection_sectioncount(ini_sections));
    OK(inisection_count(ini_sections) == 1, "section1 item count %u", inisection_count(ini_sections));
    struct KeyValue sections1_kv[] = {
        {"key1", "value1"},
        {NULL, NULL},
    };
    hashmap_foreach(ini_sections->contents, section_verify_cb, sections1_kv);
    struct IniSection *ini_sections2 = ini_sections->next;
    OK_FATAL(ini_sections2 != NULL, "have section 2");
    OK(ini_sections2->name != NULL && strcmp(ini_sections2->name, "section2") == 0, "section2 name '%s'", ini_sections2->name);
    OK(inisection_count(ini_sections2) == 1, "section2 item count %u", inisection_count(ini_sections2));
    struct KeyValue sections2_kv[] = {
        {"key2", "value2"},
        {NULL, NULL},
    };
    hashmap_foreach(ini_sections2->contents, section_verify_cb, sections2_kv);
    struct IniSection *ini_sections3 = ini_sections2->next;
    OK_FATAL(ini_sections3 != NULL, "have section 3");
    OK(ini_sections3->name != NULL && strcmp(ini_sections3->name, "section 3") == 0, "section3 name '%s'", ini_sections3->name);
    OK(inisection_count(ini_sections3) == 1, "section3 item count %u", inisection_count(ini_sections3));
    struct KeyValue sections3_kv[] = {
        {"key3", "value3"},
        {NULL, NULL},
    };
    hashmap_foreach(ini_sections3->contents, section_verify_cb, sections3_kv);
    struct IniSection *ini_sections4 = ini_sections3->next;
    OK_FATAL(ini_sections4 != NULL, "have section 4");
    OK(ini_sections4->name != NULL && strcmp(ini_sections4->name, "section name 4") == 0, "section4 name '%s'", ini_sections4->name);
    OK(inisection_count(ini_sections4) == 1, "section4 item count %u", inisection_count(ini_sections4));
    struct KeyValue sections4_kv[] = {
        {"key4", "value4"},
        {NULL, NULL},
    };
    hashmap_foreach(ini_sections4->contents, section_verify_cb, sections4_kv);
    struct IniSection *ini_sections5 = ini_sections4->next;
    OK_FATAL(ini_sections5 != NULL, "have section 5");
    OK(ini_sections5->name != NULL && strcmp(ini_sections5->name, "unicode section name っては結構") == 0, "section5 name '%s'", ini_sections3->name);
    OK(inisection_count(ini_sections5) == 0, "section5 item count %u", inisection_count(ini_sections5));
    struct IniSection *ini_sections6 = ini_sections5->next;
    OK_FATAL(ini_sections6 != NULL, "have section 6");
    OK(ini_sections6->name != NULL && strcmp(ini_sections6->name, "previous section was empty") == 0, "section6 name '%s'", ini_sections6->name);
    OK(inisection_count(ini_sections6) == 1, "section6 item count %u", inisection_count(ini_sections6));
    struct KeyValue sections6_kv[] = {
        {"key6", "value 6"},
        {NULL, NULL},
    };
    hashmap_foreach(ini_sections6->contents, section_verify_cb, sections6_kv);
    struct IniSection *ini_sections7 = ini_sections6->next;
    OK_FATAL(ini_sections7 != NULL, "have section 7");
    OK(ini_sections7->name != NULL && strcmp(ini_sections7->name, "section name with [") == 0, "section7 name '%s'", ini_sections7->name);
    OK(inisection_count(ini_sections7) == 1, "section7 item count %u", inisection_count(ini_sections7));
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
    named = inisection_find_section(NULL, NULL);
    OK(named == NULL, "find null in null");
    OK(delete_inisection(ini_sections) == NULL, "delete returns null");

    struct IniSection *ini_longline = read_inifile(SAMPLES_DIR "/good_longline.ini", &error);
    OK_FATAL(ini_longline != NULL, "longline file is valid");
    // check that we have one key,val pair in a single unnamed section
    OK(ini_longline->name == NULL, "unnamed");
    OK(inisection_sectioncount(ini_longline) == 1, "single unnamed section");
    OK(inisection_count(ini_longline) == 2, "item count %u", inisection_count(ini_longline));
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
        char *newlongkey = u_strdupcat(longkey, "verylongline");
        free(longkey);
        longkey = newlongkey;
    }
    const char *longkeyvalue = inisection_get(ini_longline, longkey);
    free(longkey);
    OK(longkeyvalue, "found longkey");
    OK(strcmp(longkeyvalue, "very long key") == 0, "longkey value");
    OK(delete_inisection(ini_longline) == NULL, "delete returns null");

    struct IniSection *ini_dosendings = read_inifile(SAMPLES_DIR "/good_dosendings.ini", &error);
    OK_FATAL(ini_dosendings != NULL, "dosendings file is valid");
    OK(ini_dosendings->name == NULL, "unnamed");
    OK(inisection_sectioncount(ini_dosendings) == 1, "one section");
    OK(inisection_count(ini_dosendings) == 2, "item count %u", inisection_count(ini_dosendings));
    struct KeyValue doskeyval[] = {
        {"key1", "val1"},
        {"key2", "val2"},
    };
    hashmap_foreach(ini_dosendings->contents, section_verify_cb, doskeyval);
    OK(ini_dosendings->next == NULL, "no more sections");
    OK(delete_inisection(ini_dosendings) == NULL, "delete returns null");
}

static void test_read_bad(void)
{
    const char *bad_samples[] = {
        SAMPLES_DIR "/bad_keyspace.ini",
        SAMPLES_DIR "/bad_nokey.ini",
        SAMPLES_DIR "/bad_novalue.ini",
        SAMPLES_DIR "/bad_sectionclose.ini",
        SAMPLES_DIR "/bad_sectionclose_noname.ini",
        SAMPLES_DIR "/bad_sectionjunk.ini",
        SAMPLES_DIR "/bad_sectionname.ini",
        SAMPLES_DIR "/bad_sectionname_empty.ini",
        SAMPLES_DIR "/bad_nonexisting.ini",
        NULL
    };

    for (unsigned i=0; bad_samples[i]; i++) {
        char *error = NULL;
        struct IniSection *ini = read_inifile(bad_samples[i], &error);
        OK(ini == NULL, "should be invalid INI");
        OK(error != NULL, "no error string");
        //printf("ini error '%s'\n", error);
        free(error);
    }
}

// returns 0 if identical
static int compare_files(const char *name1, const char *name2)
{
    FILE *f1 = fopen(name1, "r");
    FILE *f2 = fopen(name2, "r");

    if (f1 == NULL || f2 == NULL)
        return 2;

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

static void test_modify(void)
{
    struct IniSection *ini = new_inisection(NULL);
    OK_FATAL(ini != NULL, "have section");

    OK(inisection_add(ini, "key1", "value1") == 1, "add should succeed");
    OK(inisection_add(ini, "key1", "value11") == 0, "add should overwrite");
    OK(inisection_count(ini) == 1, "overwrite");
    OK(inisection_add(ini, "key2", "value2") == 1, "add should succeed");
    OK(inisection_add(ini, "key3", NULL) == 0, "NULL value should be invalid");
    OK(inisection_count(ini) == 2, "should have key1 and key2");

    OK(inisection_remove(ini, "key2") == 1, "remove should succeed");
    OK(inisection_remove(ini, "key2") == 0, "can't remove again");
    OK(inisection_count(ini) == 1, "should have key1");

    OK(delete_inisection(ini) == NULL, "delete returns null");
}

static void test_write(void)
{
    // write an ini that starts with some data then a section with some more data
    struct IniSection *ini = new_inisection(NULL);
    OK_FATAL(ini != NULL, "have section");
    inisection_add(ini, "key1", "value1");
    inisection_add(ini, "key2", "value2");
    ini->next = new_inisection("a really good section");
    struct IniSection *sec = ini->next;
    OK_FATAL(sec != NULL, "have section");
    inisection_add(sec, "key1", "value1");
    inisection_add(sec, "key2", "value2");

    const char *outputname = TEMP_DIR "/test_output.ini";
    //printf("output file name '%s'\n", outputname);
    char *error = write_inifile(outputname, ini);
    OK(error == NULL, "write error '%s'", error);
    OK(compare_files(outputname, SAMPLES_DIR "/write_good1.ini") == 0, "output matches expectation");
    remove(outputname);
    OK(delete_inisection(ini) == NULL, "delete returns null");

    // write an ini that has more sections
    ini = new_inisection("first section");
    OK_FATAL(ini != NULL, "have section");
    inisection_add(ini, "key1", "value1");
    inisection_add(ini, "key2", "value2");
    ini->next = new_inisection("a second section");
    sec = ini->next;
    OK_FATAL(sec != NULL, "have section");
    //inisection_add(sec, "key1", "value1");
    //inisection_add(sec, "key2", "value2");
    sec->next = new_inisection("wow third section");
    sec = sec->next;
    OK_FATAL(sec != NULL, "have section");
    inisection_add(sec, "key3", "value3");
    inisection_add(sec, "key4", "value4");

    error = write_inifile(outputname, ini);
    OK(error == NULL, "write error '%s'", error);
    OK(compare_files(outputname, SAMPLES_DIR "/write_good2.ini") == 0, "output matches expectation");
    remove(outputname);
    OK(delete_inisection(ini) == NULL, "delete returns null");
}

static void test_write_bad(void)
{
#define TRY(_second_valid)                                          \
    do {                                                            \
        char *error = inisection_validate(ini);                     \
        OK(error != NULL, "full ini should be invalid");            \
        /*printf("error '%s'\n", error);*/                          \
        free(error);                                                \
        error = inisection_validate(sec);                           \
        if (_second_valid) {                                        \
            OK(error == NULL, "second section should be valid");    \
        } else {                                                    \
            OK(error != NULL, "second section should be invalid");  \
            free(error);                                            \
        }                                                           \
        const char *outputname = TEMP_DIR "/test_output.ini";       \
        error = write_inifile(outputname, ini);                     \
        OK(error != NULL, "should get write error");                \
        free(error);                                                \
        FILE *of = fopen(outputname, "r");                          \
        OK(of == NULL, "the output file shouldn't exist");          \
        if (of) {                                                   \
            fclose(of);                                             \
            remove(outputname);                                     \
        }                                                           \
    } while (0)

    // initial version: second section is unnamed
    struct IniSection *ini = new_inisection("first section");
    OK_FATAL(ini != NULL, "have section");
    inisection_add(ini, "key1", "value1");
    ini->next = new_inisection(NULL);
    struct IniSection *sec = ini->next;
    OK_FATAL(sec != NULL, "have section");
    inisection_add(sec, "key1", "value1");
    TRY(1);

    // whitespace section name
    sec->name = u_strdup("      ");
    TRY(0);

    // ']' in section name
    free(sec->name);
    sec->name = u_strdup("section ] name");
    TRY(0);
    free(sec->name);
    sec->name = u_strdup("section name ]");
    TRY(0);

    // newline in section name
    free(sec->name);
    sec->name = u_strdup("section \n name");
    TRY(0);

    free(ini->name);
    free(sec->name);
    sec->name = u_strdup("valid second section name");

    // whitespace section name
    ini->name = u_strdup("      ");
    TRY(1);

    // ']' in section name
    free(ini->name);
    ini->name = u_strdup("section ] name");
    TRY(1);
    free(ini->name);
    ini->name = u_strdup("section name ]");
    TRY(1);

    // newline in section name
    free(ini->name);
    ini->name = u_strdup("section \n name");
    TRY(1);

    free(ini->name);
    ini->name = u_strdup("valid first section name");

    // whitespace in key
    inisection_add(sec, "key with whitespace", "some value");
    TRY(0);

    // newline in value
    inisection_remove(sec, "key with whitespace");
    inisection_add(sec, "newline_key", "value \n with newline");
    TRY(0);

    OK(delete_inisection(ini) == NULL, "delete returns null");
#undef TRY
}

TEST_CASES = {
    {"read", test_read},
    {"read bad", test_read_bad},
    {"modify", test_modify},
    {"write", test_write},
    {"write bad", test_write_bad},
    {NULL, NULL}
};

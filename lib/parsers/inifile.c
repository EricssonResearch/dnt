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

#include "inifile.h"
#include "hashmap.h"
#include "parserutils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#define BUFFERSIZE 1024

#define SKIP_WS(_x) while (*_x && isspace(*_x)) _x++

// @end should be the last character before null-termination
// note: newline is also trimmed
static void trim_trailing_spaces(char *start, char *end)
{
    while (end > start && isspace(*end)) {
        *end = 0;
        end--;
    }
}

struct IniSection *new_inisection(const char *name)
{
    struct IniSection *ret = (struct IniSection *)malloc(sizeof(struct IniSection));
    ret->name = u_strdup(name);
    //TODO what's a good bucket count?
    ret->contents = new_hashmap(17, NULL, NULL);
    ret->next = NULL;
    return ret;
}

struct IniSection *delete_inisection(struct IniSection *sec)
{
    while (sec) {
        free(sec->name);
        delete_hashmap(sec->contents);
        struct IniSection *del = sec;
        sec = sec->next;
        free(del);
    }
    return NULL;
}

int inisection_add(struct IniSection *sec, const char *key, const char *value)
{
    if (!sec) return 0;
    if (!key) return 0;
    if (!value) return 0;
    return hashmap_insert(sec->contents, u_strdup(key), u_strdup(value));
}

int inisection_remove(struct IniSection *sec, const char *key)
{
    if (!sec) return 0;
    return hashmap_remove(sec->contents, key);
}

char *inisection_get(const struct IniSection *sec, const char *key)
{
    if (!sec) return NULL;
    return (char *)hashmap_find(sec->contents, key);
}

unsigned inisection_count(const struct IniSection *sec)
{
    if (!sec) return 0;
    return hashmap_count(sec->contents);
}

unsigned inisection_sectioncount(const struct IniSection *sec)
{
    unsigned count = 0;
    while (sec) {
        count++;
        sec = sec->next;
    }
    return count;
}

struct IniSection *inisection_find_section(struct IniSection *sec, const char *name)
{
    if (name == NULL) return NULL;
    struct IniSection *s = sec;
    while (s) {
        if (s->name && strcmp(s->name, name) == 0)
            return s;
        s = s->next;
    }
    return NULL;
}

char *inisection_validate(const struct IniSection *sec)
{
    unsigned section = 1;

    while (sec) {
        if (sec->name) {
            char *p = sec->name;
            SKIP_WS(p);
            if (*p == 0) {
                return u_strdup_printf("section %u name '%s' is just whitespace", section, sec->name);
            }

            p = strchr(sec->name, ']');
            if (p != NULL) {
                return u_strdup_printf("section %u name '%s' contains ']'", section, sec->name);
            }

            p = strchr(sec->name, '\n');
            if (p != NULL) {
                return u_strdup_printf("section %u name '%s' contains newline", section, sec->name);
            }
        } else {
            if (section > 1) {
                return u_strdup("only the first section is allowed to be unnamed");
            }
        }

        HASHMAP_ITERATE(sec->contents, it) {
            const char *p = hash_iterator_key(&it);
            while (*p) {
                if (isspace(*p)) {
                    return u_strdup_printf("section %u '%s' key '%s' contains whitespace",
                            section, sec->name, hash_iterator_key(&it));
                }
                p++;
            }

            p = (const char *)hash_iterator_value(&it);
            while (*p) {
                if (*p == '\n') {
                    return u_strdup_printf("section %u '%s' item '%s' value contains newline",
                            section, sec->name, hash_iterator_key(&it));
                }
                p++;
            }
        }

        sec = sec->next;
        section++;
    }

    return NULL;
}

struct IniSection *read_inifile(const char *filename, char **error)
{
    *error = NULL;
    FILE *f = fopen(filename, "r");

    if (!f) {
        //TODO we should be using strerror_r for thread-safety but it's not available
        *error = u_strdup_printf("can't open: %s\n", strerror(errno));
        return NULL;
    }

#define THROW(msg, ...)                                 \
    do {                                                \
        fclose(f);                                      \
        delete_inisection(ret);                         \
        *error = u_strdup_printf("line %d error: " msg, \
                line, ##__VA_ARGS__);                   \
        free(linebuf);                                  \
        return NULL;                                    \
    } while (0)

    struct IniSection *ret = new_inisection(NULL); // points to first section
    struct IniSection *sec = ret; // points to current section
    int line = 0;

    unsigned bufsize = BUFFERSIZE;
    char *linebuf = (char *)malloc(BUFFERSIZE*sizeof(char));
    if (!linebuf) {
        THROW("memory allocation failure");
    }

    while (fgets(linebuf, BUFFERSIZE, f)) {
        unsigned len = strlen(linebuf);
        if (len == 0) continue;
        while (linebuf[len-1] != '\n') {
            // line is longer than our buffer: realloc, read more
            bufsize += BUFFERSIZE;
            char *newlinebuf = (char *)realloc(linebuf, bufsize*sizeof(char));
            if (newlinebuf) {
                linebuf = newlinebuf;
            } else {
                THROW("memory allocation failure");
            }
            char *fg = fgets(linebuf+len, BUFFERSIZE, f);
            len = strlen(linebuf);
            if (fg == NULL) {
                // reached EOF, missing \n on last line
                break;
            }
        }
        char *c = linebuf;
        line++;

        SKIP_WS(c);

        if (*c == 0) continue; // empty line
        if (*c == ';' || *c == '#') continue; // comment line

        if (*c == '[') { // section header
            c++;
            SKIP_WS(c);

            char *d = strchr(c, ']');
            if (d == NULL) {
                THROW("section header missing ]");
            }

            char *j = d+1;
            SKIP_WS(j);
            if (*j) {
                THROW("junk after section header");
            }

            *d = 0;
            trim_trailing_spaces(c, d-1);
            if (*c == 0) {
                THROW("section has no name");
            }

            if (sec->name == NULL && hashmap_count(sec->contents) == 0) {
                // file begins with a section header
                sec->name = u_strdup(c);
            } else {
                sec->next = new_inisection(c);
                sec = sec->next;
            }
        } else {
            // find the equal sign
            if (*c == '=') {
                THROW("item key missing");
            }

            char *eq = strchr(c, '=');
            if (eq == NULL) {
                THROW("no '=' sign found");
            }
            *eq = 0; // close key string
            trim_trailing_spaces(c, eq-1);

            // check for spaces in key
            char *ks = c;
            while (*ks) {
                if (isspace(*ks)) {
                    THROW("item key contains whitespace");
                }
                ks++;
            }

            // get value
            char *v = eq+1;
            SKIP_WS(v);

            // get rid of newline
            unsigned vlen = strlen(v);
            while (vlen && iscntrl(v[vlen])) {
                v[vlen] = 0;
                vlen--;
            }

            // duplicate keys: old key and value are freed
            if (hashmap_find(sec->contents, c)) {
                THROW("duplicate key '%s'", c);
            }
            hashmap_insert(sec->contents, u_strdup(c), u_strdup(v));
        }
    }

    fclose(f);
    free(linebuf);
    return ret;
#undef THROW
}

static int write_one_elem(const char *key, void *value, void *userdata)
{
    char *val = (char *)value;
    FILE *f = (FILE *)userdata;

    fprintf(f, "%s = %s\n", key, val);
    return 1;
}

char *write_inifile(const char *filename, const struct IniSection *sec)
{
    char *error = inisection_validate(sec);
    if (error)
        return error;

    FILE *f = fopen(filename, "w");

    if (!f) {
        //TODO we should be using strerror_r for thread-safety but it's not available
        return u_strdup_printf("can't open: %s", strerror(errno));
    }

    while (sec) {
        if (sec->name) {
            fprintf(f, "\n[ %s ]\n\n", sec->name);
        }
        hashmap_foreach_sorted(sec->contents, write_one_elem, f);
        sec = sec->next;
    }

    fclose(f);
    return NULL;
}


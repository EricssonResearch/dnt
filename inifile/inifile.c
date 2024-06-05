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

#include "inifile.h"
#include "hashmap.h"
#include "iniutils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#define BUFFERSIZE 1024
//#define INIFILE_QUIET

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
    ret->contents = new_hashmap(51, NULL, NULL);
    ret->next = NULL;
    return ret;
}

void delete_inisection(struct IniSection *sec)
{
    while (sec) {
        free(sec->name);
        delete_hashmap(sec->contents);
        struct IniSection *del = sec;
        sec = sec->next;
        free(del);
    }
}

void inisection_add(struct IniSection *sec, const char *key, const char *value)
{
    if (!sec) return;
    hashmap_insert(sec->contents, u_strdup(key), u_strdup(value));
}

void inisection_remove(struct IniSection *sec, const char *key)
{
    if (!sec) return;
    hashmap_remove(sec->contents, key);
}

char *inisection_get(const struct IniSection *sec, const char *key)
{
    if (!sec) return NULL;
    return (char *)hashmap_find(sec->contents, key);
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

static struct IniSection *read_error(struct IniSection *ret, const char *filename, const char *err, int line)
{
    delete_inisection(ret);
#ifndef INIFILE_QUIET
    fprintf(stderr, "read_inifile(%s) error: %s in line %d\n", filename, err, line);
#else
    (void)filename;
    (void)err;
    (void)line;
#endif
    return NULL;
}

struct IniSection *read_inifile(const char *filename)
{
    FILE *f = fopen(filename, "r");
    //printf("filename '%s'\n", filename);

    if (!f) {
#ifndef INIFILE_QUIET
        //TODO we should be using strerror_r for thread-safety but it's not available
        fprintf(stderr, "read_inifile() can't open '%s': %s\n", filename, strerror(errno));
#endif
        return NULL;
    }

    struct IniSection *ret = new_inisection(NULL); // points to first section
    struct IniSection *sec = ret; // points to current section

    unsigned bufsize = BUFFERSIZE;
    char *linebuf = (char *)malloc(BUFFERSIZE*sizeof(char));
    if (!linebuf) {
        return read_error(ret, filename, "memory allocation failure", 0);
    }

    int line = 0;
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
                free(linebuf);
                return read_error(ret, filename, "memory allocation failure", line);
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
        //printf("line %d '%s'\n", line, c);

        while (*c && isspace(*c)) c++;

        if (*c == 0) continue; // empty line
        if (*c == ';' || *c == '#') continue; // comment line

        if (*c == '[') { // section header
            c++;
            while (*c && isspace(*c)) c++;

            char *d = c;
            while (*d && *d != ']') d++;
            if (*d == 0) {
                fclose(f);
                free(linebuf);
                return read_error(ret, filename, "section header missing ]", line);
            }
            *d = 0;
            char *j = d+1;
            while (*j && isspace(*j)) j++;
            if (*j) {
                fclose(f);
                free(linebuf);
                return read_error(ret, filename, "junk after section header", line);
            }
            trim_trailing_spaces(c, d-1);
            //printf("section title '%s'\n", c);
            if (*c == 0) {
                fclose(f);
                free(linebuf);
                return read_error(ret, filename, "section has no name", line);
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
                fclose(f);
                free(linebuf);
                return read_error(ret, filename, "key missing", line);
            }

            char *eq = strchr(c, '=');
            if (eq) {
                *eq = 0; // close key string
                trim_trailing_spaces(c, eq-1);

                // check for spaces in key
                char *ks = c;
                while (*ks) {
                    if (isspace(*ks)) {
                        fclose(f);
                        free(linebuf);
                        return read_error(ret, filename, "key contains whitespace", line);
                    }
                    ks++;
                }

                // get value
                char *v = eq+1;
                while (*v && isspace(*v)) v++;

                // get rid of newline
                unsigned vlen = strlen(v);
                while (vlen && iscntrl(v[vlen])) {
                    v[vlen] = 0;
                    vlen--;
                }

                //printf("key '%s' value '%s'\n", c, v);
                // duplicate keys: old key and value are freed
                if (hashmap_find(sec->contents, c)) {
                    return read_error(ret, filename, "duplicate key", line);
                } else {
                    hashmap_insert(sec->contents, u_strdup(c), u_strdup(v));
                }
            } else {
                fclose(f);
                free(linebuf);
                return read_error(ret, filename, "no '=' sign found", line);
            }
        }
    }

    fclose(f);
    free(linebuf);
    return ret;
}

static int write_one_elem(const char *key, void *value, void *userdata)
{
    char *val = (char *)value;
    FILE *f = (FILE *)userdata;

    fprintf(f, "%s = %s\n", key, val);
    return 1;
}

int write_inifile(const char *filename, const struct IniSection *sec)
{
    FILE *f = fopen(filename, "w");

    if (!f) {
#ifndef INIFILE_QUIET
        //TODO we should be using strerror_r for thread-safety but it's not available
        fprintf(stderr, "write_inifile() can't open '%s': %s\n", filename, strerror(errno));
#endif
        return 1;
    }

    int firstsection = 1;
    while (sec) {
        if (sec->name) {
            fprintf(f, "\n[ %s ]\n\n", sec->name);
        } else {
            if (!firstsection) {
                fclose(f);
#ifndef INIFILE_QUIET
                fprintf(stderr, "write_inifile('%s') only the first section is allowed to be unnamed\n", filename);
#endif
                return 1;
            }
        }

        hashmap_foreach_sorted(sec->contents, write_one_elem, f);

        firstsection = 0;
        sec = sec->next;
    }

    fclose(f);
    return 0;
}


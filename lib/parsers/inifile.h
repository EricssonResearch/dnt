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

#ifndef INIFILE_H
#define INIFILE_H

//TODO support localization: key[lang]=value

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Read/write INI files.
 *
 * Lines beginning with # or ; are considered comments, and are ignored.
 * Empty lines are ignored.
 *
 * Data lines are like "key=value". Leading whitespaces and whitespaces around
 * the = sign are ignored, so "  key   = value" is equivalent to the above one.
 * Keys can contain any non-whitespace character.
 *
 * Section headers are supported in the format "[Section name]". Whitespace
 * before and after the [ and ] delimiters are ignored, so " [ Section name ] "
 * is equivalent to he above one. The section name is allowed to contain
 * whitespaces, but has to contain at least one non-whitespace character.
 *
 * In the IniSection structure @name is NULL for unnamed sections. Obviously,
 * only the first section can be unnamed.
 */

#include "hashmap.h"

struct IniSection {
    char *name;
    struct HashMap *contents;
    struct IniSection *next;
};

// creates a new ini section
struct IniSection *new_inisection(const char *name);

// also deletes the next sections in the chain
void delete_inisection(struct IniSection *sec);

// adds the (key, value) pair to the section, overrides any existing value
// the duplicates of the given strings are added to the hash map
void inisection_add(struct IniSection *sec, const char *key, const char *value);

// removes the key from the section
void inisection_remove(struct IniSection *sec, const char *key);

// finds the value for the given key, returns NULL if not found
char *inisection_get(const struct IniSection *sec, const char *key);

// finds the section that has the given name
struct IniSection *inisection_find_section(struct IniSection *sec, const char *name);

// sections are ordered as they were in the file
// contents of the sections are ordered by the hash function
struct IniSection *read_inifile(const char *filename);

// first section is allowed to have NULL name
// returns 0 on success or an error code
// the output file is left incomplete when an error is detected
int write_inifile(const char *filename, const struct IniSection *sec);

#ifdef __cplusplus
}
#endif

#endif // INIFILE_H

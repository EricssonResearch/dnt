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

#ifndef INIFILE_H
#define INIFILE_H

/**
 * Read/write INI files.
 *
 * Lines beginning with # or ; are considered comments, and are ignored.
 * Empty lines are ignored.
 *
 * Data lines are like "key=value". Leading whitespaces and whitespaces around
 * the = sign are ignored, so "  key   = value" is equivalent to the one above.
 * Keys can contain any non-whitespace character.
 *
 * Keys are case-sensitive, which is a slight deviation from the original INI
 * format. There is no official specification for this format, though.
 *
 * Duplicate keys in a section are treated as error.
 *
 * Section headers are supported in the format "[Section name]". Whitespace
 * before and after the [ and ] delimiters are ignored, so " [ Section name ] "
 * is equivalent to the one above. The section name is allowed to contain
 * whitespaces, but has to contain at least one non-whitespace character.
 *
 * In the IniSection structure @name is NULL for unnamed sections. Obviously,
 * only the first section can be unnamed.
 */

#include "hashmap.h"

#ifdef __cplusplus
extern "C" {
#endif

struct IniSection {
    char *name;
    struct HashMap *contents;
    struct IniSection *next;
};

// creates a new ini section
// duplicates @name
// @name can be NULL (only for the first one in a chain)
struct IniSection *new_inisection(const char *name);

// also deletes the next sections in the chain
// always @returns NULL
struct IniSection *delete_inisection(struct IniSection *sec);

// adds the (key, value) pair to the section
// neither @key nor @value can be NULL
// overwrites any existing value
// the duplicates of the given strings are added to the hash map
// @returns 1 if a new item was inserted
// @returns 0 on error or an existing item was overwritten
int inisection_add(struct IniSection *sec, const char *key, const char *value);

// removes the item with this @key from the section
// @returns 1 if an item was deleted, 0 otherwise
int inisection_remove(struct IniSection *sec, const char *key);

// finds the value for the given key, returns NULL if not found
char *inisection_get(const struct IniSection *sec, const char *key);

// @returns the number of items in this section
unsigned inisection_count(const struct IniSection *sec);

// @returns the number of sections (starting with @sec)
unsigned inisection_sectioncount(const struct IniSection *sec);

// finds the section that has the given name
struct IniSection *inisection_find_section(struct IniSection *sec, const char *name);

// @returns a dynamically allocated error string if @sec contains invalid data
//  - whitespace in item key
//  - newline in section name or value
//  - ] in section name
//  - unnamed (or all whitespace) not-first section
char *inisection_validate(const struct IniSection *sec);

// section chain is ordered as they were in the file
// the contents of the sections are unordered
// @returns a handle to the first section, or NULL on error
// @error is set to a dynamically allocated error string on error
struct IniSection *read_inifile(const char *filename, char **error);

// @returns NULL on success, or a dynamically allocated error string
// the items in the sections are ordered by their keys for reproducible results
// validates @sec before attempting to write the output
char *write_inifile(const char *filename, const struct IniSection *sec);

#ifdef __cplusplus
}
#endif

#endif // INIFILE_H

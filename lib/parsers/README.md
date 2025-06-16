# Parsers for some file formats

This library contains an INI parser and a JSON parser for serializing/deserializing data, and a Hashmap that is used by both. The interfaces of the parsers differ, because they come from different projects. Both parsers have read and write support.

All the code is standard C99 without dependencies. MIT license.

There are unit tests in the `test` directory, they also show how the library can be used. Building the tests requires `cmake`. An example of building and running the tests:

```sh
mkdir test/build
cd test/build
cmake ..
make
valgrind ./hashmap
```

The unit tests use the testing framework from [here](https://gitlab.com/mtmkls/testing).

There are example applications in the `linters` directory. They read files, and decide if the format of the files are valid. These can also be used for fuzz-testing the library.

# Hashmap

A string-keyed hash table, also called dictionary. Supports NULL values, so it can also be used as a set.

It supports changing the bucket count, but doesn't do it automatically.

The hash owns the key and the value inserted into it. Inserting a new value with a key that is the same as an existing item overwrites the existing item. When deleting or overwriting items the hash uses the delete callback supplied to the constructor. The default delete callback uses `free()` on both the key and the value.

There are two interfaces for iterating the hash: a callback-based foreach, and an iterator-based looping. The foreach loop supports deleting the actual item in the callback. The sorted foreach and the iterator don't support changing the hash that is being iterated. The sorted foreach uses the builtin C `qsort()` function.

# INI

Reads and writes files in the INI format. Supports single-line `key=value` items, `[section headers]`, comment lines beginning with `#` or `;` characters. Whitespaces around the `=` and the brackets are ignored. Multi-line values are not supported. Duplicate keys within a section are treated as error.

The keys are case-sensitive, because this was originally meant to read [.desktop files](https://specifications.freedesktop.org/desktop-entry-spec/), which are specified to be case-sensitive. Normal INI files are sometimes not case-sensitive, though.

The in-memory representation is a linked list of the sections, the items in each section are stored in a hash (section order is preserved, item order is not). When writing only the first section can be unnamed.

# JSON

Reads data in [JSON format](https://www.json.org/json-en.html) from non-null-terminated strings. It was originally created to read data from messages in Type-Length-Value encoding, where the value contained no null termination. When writing JSON the output string is null-terminated. Dense and formatted output are both supported.

The parser is recursive with limited recursion depth. It supports all JSON features according to the ECMA-404 standard.

Notes:

* The parser reads until the end of the input buffer, and doesn't stop at a \0 character. The \0 character is only allowed as the last byte of the buffer.

* ECMA-404 says object keys are not required to be unique. This parser returns objects that have unique keys -- if the input has multiple items with the same key, only the first one is kept.

* Printing and scanning numbers depend on the current locale settings in the application (see the `LC_NUMERIC` category). By default the locale is "C", which is good, so this is only relevant if your application explicitly sets a non-default locale, or you are using a framework that automatically adopts the system locale from the environment (e.g. GTK+). If unsure, use the `json_check_locale` function to see if the current locale is suitable.

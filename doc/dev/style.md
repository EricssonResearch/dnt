
# Coding Style

We use the One True indentation format: Kernighan & Ritchie.
Indentation is 4 spaces, maximum line length is not enforced, but should not exceed 100 characters.
There should be no trailing whitespaces.
These settings are included in the `.editorconfig` file, see [EditorConfig](https://editorconfig.org/) on how to set up your editor to use it.

The code is written in standard C, conforming to the gnu99 dialect.
It must also be compatible with C++ of the gnu++03 dialect.
In addition to this, all reasonable warnings are turned on, and the code must compile without warnings.
These rules create a safer subset of C.

Headers must be self-contained, the include order must not matter.
To ensure this, all compilation units must include their own header first.

The preferred header include order is: own header, project headers, standard library headers, system headers.
Alphabetic ordering within those groups is preferred.

Alphabetic ordering is preferred everywhere.

The public functions in the `.c` files should be in the same order as they are in the corresponding header file.
The static functions should all go before the public ones, if possible.
Pre-declaring static functions should be avoided, keep everything in calling order.

Structures, enums and pointer types are not typedef-ed.
This makes it easier to see what is what.

Public API of the modules should be minimal and as generic as possible.
The public methods should be prefixed by the name of the module.

Avoid global variables and public variables.

DNT runs with many threads, *everything* should be thread-safe.


## Memory management conventions

Object delete methods should return a NULL pointer.
This enables the `pSomething = delete_something(pSomething);` idiom.

Objects that are referenced by multiple other objects must be reference-counted.
When an object lookup function returns a pointer to an item, it must increase the reference count.
References must be released on objects that are no longer used, we don't like memory leaks.

Per-packet memory leaks are absolutely forbidden.

Use [Valgrind](https://valgrind.org/) to make sure there are no illegal memory accesses.


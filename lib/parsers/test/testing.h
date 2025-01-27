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

/*
 * testing.h is a simple unit test framework for C and C++ projects
 *
 * This framework aims to be simple, easy to use, and free of dependencies.
 * The only thing needed is a C compiler with at least C99 support or a C++
 * compiler with at least C++98 support.
 *
 * Let's suppose you have a project that contains a module (foo.c and foo.h)
 * that has functions you want to unit test. You need to
 * create a footest.c file that contains:
 *
 *   #include "testing.h"
 *   #include "foo.h"
 *
 *   TEST_INIT("Foo Module");
 *
 *   static void test_foo1(void)
 *   {
 *      OK(foo1(42) == 42, "the ultimate answer");
 *   }
 *
 *   static void test_foo2(void)
 *   {
 *      OK(foo2(42) == 0, "must return 0");
 *   }
 *
 *   TEST_CASES = {
 *      {"foo1", test_foo1},
 *      {"foo2", test_foo2},
 *      {NULL, NULL}
 *   };
 *
 * The TEST_INIT() initializes this file to be the main file of the test.
 * The TEST_CASES list contains the test cases that will be run. These two
 * are the only mandatory parts of the test, and they have to be in the
 * same file, in any order.
 *
 * The test functions can use the helpers from testing.h to examine the
 * module under test, and report success/failure.
 *
 * Do not put TEST_INIT and TEST_CASES in the file you are testing! The tests
 * should go into a separate compilation unit.
 *
 * Neither the module under test nor the test file should contain a main()
 * function! That is provided by the test framework.
 *
 * To compile&run the above example:
 *
 *   gcc -Wall -g -DTESTING foo.c footest.c -o footest
 *   valgrind --error-exitcode=42 ./footest
 *
 * The test runs all the test cases, counts the number of successes and
 * failures, prints a summary at the end of each test case, and the totals at
 * the end of the test. You can check the results of the test when running
 * from a script: the return value of the test executable is 0 when no test
 * case failed, and 1 if there were failures.
 *
 * By default the output is colorful only if stdout is a terminal. This can be
 * overridden by creating an environment variable named TEST_COLOR with any
 * value.
 *
 * The following test helper macros are available:
 *
 *   OK(condition, message)
 *      if the condition is false, it's a failure, and the message is printed
 *   OK_FATAL(condition, message)
 *      same as OK(), but aborts the current test case on failure
 *      useful for sanity checks, like memory allocation
 *   FAIL(message)
 *      unconditional failure (e.g. the callback wasn't supposed to called)
 *   FAIL_FATAL(message)
 *      unconditional failure and abort the current test case
 *   ASSERTS(expression, message)
 *      expression is expected to fail on an assert() call
 *      always continues the current test case
 *   ASSERTS_FATAL(expression, message)
 *      same as ASSERTS() but aborts the current test case on failure
 *   SKIP(message)
 *      skips the rest of the test case without registering a failure
 *      useful for unfinished or unsupported test cases
 *
 * The message parameter can contain printf patterns and additional parameters
 * to be substituted. The printf substitution will be performed only when the
 * test fails.
 *
 * An assertion failure outside of ASSERTS() doesn't exit the program, just the
 * current test case.
 *
 * In C++ mode the following helpers are also available:
 *
 *   EXCEPTS(expression, exception_type, message)
 *      the expression is expected to throw an exception with the given type
 *   EXCEPTS_FATAL(expression, exception_type, message)
 *      same as EXCEPTS(), but aborts the current test case on failure
 *
 * Throwing an uncaught exception outside EXCEPTS() doesn't exit the program,
 * just the current test case.
 *
 * Advanced usage:
 *
 * The test can spread over multiple source files, but there has to be a main
 * one that contains TEST_INIT and TEST_CASES. Only source files that use OK()
 * and such need to include testing.h.
 *
 * The testing macros are no-op if TESTING is not defined. It's safe to call
 * OK() from your regular code, it will only be activated during the test.
 * Note that the condition of OK() is not executed either, so it's not a good
 * idea to use a condition that has important side-effects.
 *
 */

#ifndef TESTING_H
#define TESTING_H

#ifdef TESTING

#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <assert.h>

// get isatty()
#ifdef _WIN32
// note: you need a fairly recent version of Windows 10
// and probably some regedit-hacking
// to have colors on the console
#include <io.h>
#define TTY_TEST _isatty(_fileno(stdout))
#else
#include <unistd.h>
#define TTY_TEST isatty(STDOUT_FILENO)
#endif

//TODO the variadic macros prevent us from using -Wpedantic

#define OK(_condition, _description, ...) do {                          \
    if (_condition) {                                                   \
        _test_succ++;                                                   \
    } else {                                                            \
        _test_fail++;                                                   \
        printf(_test_color ?                                            \
                "\n    %s %d \033[1;31mFAILED\033[0m: " _description :  \
                "\n    %s %d FAILED: " _description,                    \
                __FILE__, __LINE__, ##__VA_ARGS__);                     \
    }                                                                   \
} while (0)

#define OK_FATAL(_condition, _description, ...) do {                    \
    if (_condition) {                                                   \
        _test_succ++;                                                   \
    } else {                                                            \
        _test_fail++;                                                   \
        printf(_test_color ?                                            \
                "\n    %s %d \033[1;31mFAILED\033[0m: " _description :  \
                "\n    %s %d FAILED: " _description,                    \
                __FILE__, __LINE__, ##__VA_ARGS__);                     \
        longjmp(_test_jmp_buffer, 9);                                   \
    }                                                                   \
} while (0)

#define FAIL(_description, ...) OK(0, _description, ##__VA_ARGS__)

#define FAIL_FATAL(_description, ...) OK_FATAL(0, _description, ##__VA_ARGS__)

#define _ASSERTS(_reaction, _expression, _description, ...) do {            \
    _test_assert_count = 0;                                                 \
    _test_needs_assert = 1;                                                 \
    _expression;                                                            \
    _test_needs_assert = 0;                                                 \
    if (_test_assert_count) {                                               \
        _test_succ++;                                                       \
    } else {                                                                \
        _test_fail++;                                                       \
        printf(_test_color ?                                                \
                "\n    %s %d \033[1;31mNO ASSERT\033[0m: " _description :   \
                "\n    %s %d NO ASSERT: " _description,                     \
                __FILE__, __LINE__, ##__VA_ARGS__);                         \
        _reaction;                                                          \
    }                                                                       \
} while(0)

#define ASSERTS(_expression, _description, ...)         \
     _ASSERTS((void)0, _expression, _description, ##__VA_ARGS__)

#define ASSERTS_FATAL(_expression, _description, ...)   \
     _ASSERTS(longjmp(_test_jmp_buffer, 9), _expression, _description, ##__VA_ARGS__)

#define SKIP(_description, ...) do {                        \
    printf(_test_color ?                                    \
            " \033[1;33mSKIP\033[0m " _description:         \
            " SKIP " _description,                          \
            ##__VA_ARGS__);                                 \
    longjmp(_test_jmp_buffer, 9);                           \
} while (0)

#undef assert
//TODO C23 changes the signature to assert(...)
#define assert(_condition) do {                             \
    if (!(_condition)) {                                    \
        if (_test_needs_assert) {                           \
            _test_assert_count++;                           \
        } else {                                            \
            _test_fail++;                                   \
            printf(_test_color ?                            \
                    "\n    \033[1;31mASSERT\033[0m: %s" :   \
                    "\n    ASSERT: %s",                     \
                    #_condition);                           \
            longjmp(_test_jmp_buffer, 9);                   \
        }                                                   \
    }                                                       \
} while (0)

#ifdef __cplusplus

#ifdef __GLIBC__
#include <cxxabi.h>

static inline char *_exTypeName()
{
    int status;
    return abi::__cxa_demangle(abi::__cxa_current_exception_type()->name(), 0, 0, &status);
}
#else
static inline char *_exTypeName() { return NULL; }
#endif

#define _CALL_TEST                                                              \
     try { _test_case_list[i].fn(); }                                           \
     catch(...) {                                                               \
         _test_fail++;                                                          \
         char *_xtype = _exTypeName();                                          \
         printf(_test_color ?                                                   \
                 "\n    \033[1;31mUNEXPECTED EXCEPTION\033[0m%s%s%s" :          \
                 "\n    UNEXPECTED EXCEPTION%s%s%s",                            \
                 _xtype ? " '" : "", _xtype ? _xtype : "", _xtype ? "' " : ""   \
                 );                                                             \
         free(_xtype);                                                          \
     }

#define _EXCEPTS(_reaction, _expression, _xcept_type, _description, ...) do {           \
    bool _caught_xc = false;                                                            \
    try { _expression; }                                                                \
    catch (_xcept_type) { _caught_xc = true; _test_succ++; }                            \
    catch (...) {                                                                       \
        _caught_xc = true; _test_fail++;                                                \
        char *_xtype = _exTypeName();                                                   \
        printf(_test_color ?                                                            \
                "\n    %s %d \033[1;31mWRONG EXCEPTION\033[0m%s%s%s: " _description :   \
                "\n    %s %d WRONG EXCEPTION%s%s%s: " _description,                     \
                __FILE__, __LINE__,                                                     \
                _xtype ? " '" : "", _xtype ? _xtype : "", _xtype ? "' " : "",           \
                ##__VA_ARGS__);                                                         \
        free(_xtype);                                                                   \
        _reaction;                                                                      \
    }                                                                                   \
    if (!_caught_xc) {                                                                  \
        _test_fail++;                                                                   \
        printf(_test_color ?                                                            \
                "\n    %s %d \033[1;31mNO EXCEPTION\033[0m: " _description :            \
                "\n    %s %d NO EXCEPTION: " _description,                              \
                __FILE__, __LINE__, ##__VA_ARGS__);                                     \
        _reaction;                                                                      \
    }                                                                                   \
} while (0)

#define EXCEPTS(_expression, _xcept_type, _description, ...) \
     _EXCEPTS((void)0, _expression, _xcept_type, _description, ##__VA_ARGS__)

#define EXCEPTS_FATAL(_expression, _xcept_type, _description, ...) \
     _EXCEPTS(longjmp(_test_jmp_buffer, 9), _expression, _xcept_type, _description, ##__VA_ARGS__)

#else

#define _CALL_TEST _test_case_list[i].fn();

#endif // __cplusplus

extern int _test_color;
extern int _test_succ;
extern int _test_fail;
extern int _test_needs_assert;
extern int _test_assert_count;
extern int _test_total_succ;
extern int _test_total_fail;
extern jmp_buf _test_jmp_buffer;

typedef void _test_function(void);
struct TestCase {
    const char *name;
    _test_function *fn;
};

#define TEST_INIT(_name)                                                    \
extern struct TestCase _test_case_list[];                                   \
                                                                            \
int _test_color = 0;                                                        \
int _test_succ = 0;                                                         \
int _test_fail = 0;                                                         \
int _test_needs_assert = 0;                                                 \
int _test_assert_count = 0;                                                 \
int _test_total_succ = 0;                                                   \
int _test_total_fail = 0;                                                   \
jmp_buf _test_jmp_buffer;                                                   \
                                                                            \
int main(void)                                                              \
{                                                                           \
    _test_color = TTY_TEST;                                                 \
    if (getenv("TEST_COLOR")) _test_color = 1;                              \
                                                                            \
    printf(_test_color ?                                                    \
            "\033[0mRunning test \033[1;36m%s\033[0m ...\n" :               \
            "Running test '%s' ...\n",                                      \
            _name);                                                         \
                                                                            \
    for (int i=0; _test_case_list[i].name; i++) {                           \
        _test_succ = 0;                                                     \
        _test_fail = 0;                                                     \
                                                                            \
        printf(_test_color ?                                                \
                "  Case \033[0;34m%s\033[0m ... " :                         \
                "  Case '%s' ... ",                                         \
                _test_case_list[i].name);                                   \
        if (setjmp(_test_jmp_buffer) == 0) {                                \
            _CALL_TEST                                                      \
        }                                                                   \
        if (_test_fail) {                                                   \
            printf(_test_color ?                                            \
                    "\n    Success rate: \033[0;31m%d/%d\033[0m\n" :        \
                    "\n    Success rate: %d/%d\n",                          \
                    _test_succ, _test_succ + _test_fail);                   \
        } else {                                                            \
            printf(_test_color ?                                            \
                    " \033[0;32m%d/%d\033[0m\n" :                           \
                    " %d/%d\n",                                             \
                    _test_succ, _test_succ + _test_fail);                   \
        }                                                                   \
                                                                            \
        _test_total_succ += _test_succ;                                     \
        _test_total_fail += _test_fail;                                     \
    }                                                                       \
                                                                            \
    if (_test_color) {                                                      \
        if (_test_total_fail)                                               \
            printf("  Total: \033[1;31m%d/%d\033[0m\n",                     \
                    _test_total_succ, _test_total_succ + _test_total_fail); \
        else                                                                \
            printf("  Total: \033[1;32m%d/%d\033[0m\n",                     \
                    _test_total_succ, _test_total_succ + _test_total_fail); \
    } else {                                                                \
        printf("  Total: %d/%d\n",                                          \
                _test_total_succ, _test_total_succ + _test_total_fail);     \
    }                                                                       \
    return !!_test_total_fail;                                              \
}                                                                           \
struct test_init_eat_semicolon_

#define TEST_CASES struct TestCase _test_case_list[]

#else

static inline void _test_eat_msg(const char *desc, ...) { (void)desc; }

#define OK(_condition, _description, ...) do { \
    _test_eat_msg(_description, ##__VA_ARGS__); \
} while (0)
#define OK_FATAL(_condition, _description, ...) do { \
    _test_eat_msg(_description, ##__VA_ARGS__); \
} while (0)
#define FAIL(_description, ...) OK(0, _description, ##__VA_ARGS__)
#define ASSERTS(_expression, ...) do { _expression; } while (0)

#include <assert.h>

#endif // TESTING

#endif // TESTING_H

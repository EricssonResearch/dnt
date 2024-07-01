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
 * testing.h is a simple unit test framework for C
 *
 * This framework aims to be simple, easy to use, and free of dependencies.
 * The only thing needed is a C compiler with at least C99 support.
 *
 * Let's suppose you have a project that contains module (foo.c and foo.h)
 * that has functions foo1() and foo2() you want to unit test. You need to
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
 *      OK(foo2(42) == 0, "returns 0");
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
 * same file. The individual test functions can use the helpers from testing.h
 * to run the functions of the module under test, and report success/failure.
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
 *   ./footest
 *
 * The test runs all the test cases, counts the number of successes and
 * failures, prints a summary at the end of each test case, and the totals at
 * the end of the test. You can check the results of the test when running
 * from a script: the return value of the test executable is 0 when no test
 * case failed, and 1 if there were failures.
 *
 * The following test helper macros are available:
 *
 *   OK(condition, message)
 *      if the condition is false, the message will be printed
 *   OK_FATAL(condition, message)
 *      same as OK(), but aborts the current test case on failure
 *      useful for sanity checks, like memory allocation
 *   FAIL(message)
 *      unconditional failure (e.g. the callback wasn't supposed to called)
 *   FAIL_FATAL(message)
 *      unconditional failure and abort the current test case
 *   test_assert(condition)
 *      unlike assert() this just aborts the current test case
 *      if TESTING is not defined, this is an alias to assert()
 *   ASSERTS(expression, message)
 *      the expression is expected to call test_assert(), fail if it doesn't
 *   ASSERTS_FATAL(expression, message)
 *      same as ASSERTS() but also aborts the current test case
 *   SKIP(message)
 *      skips the rest of the test case without registering a failure
 *
 * The message parameter can contain printf patterns and additional parameters
 * to be substituted. The printf substitution will be performed only when the
 * test fails.
 *
 * Advanced usage:
 *
 * The test can spread over multiple source files, but there has to be a main
 * one that contains TEST_INIT and TEST_CASES. Only source files that use OK()
 * need to include testing.h.
 *
 * The testing macros are no-op if TESTING is not defined. It's safe to call
 * OK() from your regular code, it will only be activated during the test.
 * Without TESTING the test_assert() call is the same as regular assert().
 *
 */

#ifndef TESTING_H
#define TESTING_H

#ifdef TESTING

#include <stdio.h>
#include <setjmp.h>

// get isatty()
#ifdef _WIN32
// note: you need a fairly recent version of Windows 10
// to be able to see colors on the console
#include <io.h>
// static inline int isatty(int fh) { (void)fh; return 0; }
#else
#include <unistd.h>
#endif

//TODO the variadic macros prevent us from using -Wpedantic

#define OK(_condition, _description, ...) do {                      \
    if (_condition) {                                               \
        test_succ++;                                                \
    } else {                                                        \
        test_fail++;                                                \
        char _msg[512];                                             \
        snprintf(_msg, sizeof(_msg), _description, ##__VA_ARGS__);  \
        _msg[511] = 0;                                              \
        printf(test_color ?                                         \
                "\n    %s %d \033[1;31mFAILED\033[0m: %s" :         \
                "\n    %s %d FAILED: %s",                           \
                __FILE__, __LINE__, _msg);                          \
    }                                                               \
} while (0)

#define OK_FATAL(_condition, _description, ...) do {                \
    if (_condition) {                                               \
        test_succ++;                                                \
    } else {                                                        \
        test_fail++;                                                \
        char _msg[512];                                             \
        snprintf(_msg, sizeof(_msg), _description, ##__VA_ARGS__);  \
        _msg[511] = 0;                                              \
        printf(test_color ?                                         \
                "\n    %s %d \033[1;31mFAILED\033[0m: %s" :         \
                "\n    %s %d FAILED: %s",                           \
                __FILE__, __LINE__, _msg);                          \
        longjmp(test_jmp_buffer, 9);                                \
    }                                                               \
} while (0)

#define FAIL(_description, ...) OK(0, _description, ##__VA_ARGS__)

#define FAIL_FATAL(_description, ...) OK_FATAL(0, _description, ##__VA_ARGS__)

// to use this, the code under test needs to call test_assert()
#define _ASSERTS(_reaction, _expression, _description, ...) do {    \
    test_assert_count = 0;                                          \
    test_needs_assert = 1;                                          \
    _expression;                                                    \
    test_needs_assert = 0;                                          \
    if (test_assert_count) {                                        \
        test_succ++;                                                \
    } else {                                                        \
        test_fail++;                                                \
        char _msg[512];                                             \
        snprintf(_msg, sizeof(_msg), _description, ##__VA_ARGS__);  \
        _msg[511] = 0;                                              \
        printf(test_color ?                                         \
                "\n    %s %d \033[1;31mNO ASSERT\033[0m: %s" :      \
                "\n    %s %d NO ASSERT: %s",                        \
                __FILE__, __LINE__, _msg);                          \
        _reaction;                                                  \
    }                                                               \
} while(0)

#define ASSERTS(_expression, _description, ...)         \
     _ASSERTS((void)_msg, _expression, _description, ##__VA_ARGS__)

#define ASSERTS_FATAL(_expression, _description, ...)   \
     _ASSERTS(longjmp(test_jmp_buffer, 9), _expression, _description, ##__VA_ARGS__)

#define SKIP(_description, ...) do {                            \
    char _msg[512];                                             \
    snprintf(_msg, sizeof(_msg), _description, ##__VA_ARGS__);  \
    _msg[511] = 0;                                              \
    printf(test_color ?                                         \
            " \033[1;33mSKIP\033[0m %s " :                      \
            " SKIP %s ",                                        \
            _msg);                                              \
    longjmp(test_jmp_buffer, 9);                                \
} while (0)

#define test_assert(_condition) do {                        \
    if (!(_condition)) {                                    \
        if (test_needs_assert) {                            \
            test_assert_count++;                            \
        } else {                                            \
            test_fail++;                                    \
            printf(test_color ?                             \
                    "\n    \033[1;31mASSERT\033[0m: %s" :   \
                    "\n    ASSERT: %s",                     \
                    #_condition);                           \
            longjmp(test_jmp_buffer, 9);                    \
        }                                                   \
    }                                                       \
} while (0)

extern int test_color;
extern int test_succ;
extern int test_fail;
extern int test_needs_assert;
extern int test_assert_count;
extern int test_total_succ;
extern int test_total_fail;
extern jmp_buf test_jmp_buffer;

typedef void test_function(void);
struct TestCase {
    const char *name;
    test_function *fn;
};

#define TEST_INIT(_name)                                                \
extern struct TestCase test_case_list[];                                \
                                                                        \
int test_color = 0;                                                     \
int test_succ = 0;                                                      \
int test_fail = 0;                                                      \
int test_needs_assert = 0;                                              \
int test_assert_count = 0;                                              \
int test_total_succ = 0;                                                \
int test_total_fail = 0;                                                \
jmp_buf test_jmp_buffer;                                                \
                                                                        \
int main(void)                                                          \
{                                                                       \
    test_color = isatty(STDOUT_FILENO);                                 \
                                                                        \
    printf(test_color ?                                                 \
            "\033[0mRunning test \033[1;36m%s\033[0m ...\n" :           \
            "Running test '%s' ...\n",                                  \
            _name);                                                     \
                                                                        \
    for (int i=0; test_case_list[i].name; i++) {                        \
        test_succ = 0;                                                  \
        test_fail = 0;                                                  \
                                                                        \
        printf(test_color ?                                             \
                "  Case \033[0;34m%s\033[0m ... " :                     \
                "  Case '%s' ... ",                                     \
                test_case_list[i].name);                                \
        if (setjmp(test_jmp_buffer) == 0) {                             \
            test_case_list[i].fn();                                     \
        }                                                               \
        if (test_fail) {                                                \
            printf(test_color ?                                         \
                    "\n    Success rate: \033[0;31m%d/%d\033[0m\n" :    \
                    "\n    Success rate: %d/%d\n",                      \
                    test_succ, test_succ+test_fail);                    \
        } else {                                                        \
            printf(test_color ?                                         \
                    " \033[0;32m%d/%d\033[0m\n" :                       \
                    " %d/%d\n",                                         \
                    test_succ, test_succ+test_fail);                    \
        }                                                               \
                                                                        \
        test_total_succ += test_succ;                                   \
        test_total_fail += test_fail;                                   \
    }                                                                   \
                                                                        \
    if (test_color) {                                                   \
        if (test_total_fail)                                            \
            printf("  Total: \033[1;31m%d/%d\033[0m\n",                 \
                    test_total_succ, test_total_succ+test_total_fail);  \
        else                                                            \
            printf("  Total: \033[1;32m%d/%d\033[0m\n",                 \
                    test_total_succ, test_total_succ+test_total_fail);  \
    } else {                                                            \
        printf("  Total: %d/%d\n",                                      \
                test_total_succ, test_total_succ+test_total_fail);      \
    }                                                                   \
    return !!test_total_fail;                                           \
}

#define TEST_CASES struct TestCase test_case_list[]

#else

static inline void _test_eat_msg(const char *desc, ...) { (void)desc; }

#define OK(_condition, _description, ...) do { \
    if (_condition) {} _test_eat_msg(_description, ##__VA_ARGS__); \
} while (0)
#define OK_FATAL(_condition, _description, ...) do { \
    if (_condition) {} _test_eat_msg(_description, ##__VA_ARGS__); \
} while (0)
#define FAIL(_description, ...) OK(0, _description, ##__VA_ARGS__)
#define ASSERTS(_expression, ...) do { _expression; } while (0)

#include <assert.h>

#define test_assert assert

#endif // TESTING

#endif // TESTING_H

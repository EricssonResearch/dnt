// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_LOG_H
#define R2_LOG_H

#include <stdbool.h>

#ifdef __cplusplus
    extern "C" {
#endif
void perror(const char *s) __attribute__((deprecated));
#ifdef __cplusplus
    }
#endif

typedef enum {
    //TODO these need prefix (syslog squats on LOG_xxx)
    NONE=0,
    ERROR,
    WARNING,
    INFO,
    PACKET,
    DEBUG,
    ALL
} LOGGING_LEVELS;

typedef enum {
    LOG_OUT_STDOUT = 0,
    LOG_OUT_STDERR,
    LOG_OUT_SYSLOG,
    LOG_OUT_LOGFILE,
} LOG_OUTPUT;

/**
 * @brief Function to initialize the logger.
 *
 * @params[in] out Log output (stdout/logfile/syslog).
 * @params[in] filename Path to the log file (in case of logfile output)
 *
 * @retval true if successful.
 */
bool open_log(LOG_OUTPUT out, char *log_filename);

/* Close the log facility.
 * Closes the logfile
 * */
void close_log(void);

// @returns true of the string is a valid log level
bool log_level_valid(const char *level);

// unknown level is translated to NONE
LOGGING_LEVELS log_level_from_string(const char *level);

// @returns a string representation of the given @level
const char *log_string_from_level(LOGGING_LEVELS level);

struct __log_module {
    const char *name;
    LOGGING_LEVELS level;
};

void __register_log_module(const char *file, struct __log_module *mod);

int __log_func(LOGGING_LEVELS level, const char *logmodule, const char *frmt, ...)
    __attribute__((format(printf, 3, 4)))
    __attribute__((nonnull(2)));

int __log_p_func(LOGGING_LEVELS level, const char *logmodule, const char *frmt, ...)
    __attribute__((format(printf, 3, 4)))
    __attribute__((nonnull(2)));

// declare a module name and default level limit for logging
// the same module name can be declared in multiple compilation units
#define LOGGING_MODULE(_name, _default_level)                               \
static struct __log_module __log_module_##_name = {#_name, _default_level}; \
static void __attribute__((constructor)) register_module_##_name(void) {    \
    __register_log_module(__FILE__, &__log_module_##_name);                 \
}                                                                           \
struct require_a_semicolon

// declare a module for logging that can be used without supplying the name
// only one default can be declared in a compilation unit
#define DEFAULT_LOGGING_MODULE(_name, _default_level)                       \
static struct __log_module __log_module_##_name = {#_name, _default_level}; \
static struct __log_module *__default_log_module = &__log_module_##_name;   \
static void __attribute__((constructor)) register_module_##_name(void) {    \
    __register_log_module(__FILE__, __default_log_module);                  \
}                                                                           \
struct require_a_semicolon

#define log_debug_m(_name, ...)                                             \
    if (__log_module_##_name.level >= DEBUG)                                \
        __log_func(DEBUG, __log_module_##_name.name, ##__VA_ARGS__)

#define log_packet_m(_name, ...)                                            \
    if (__log_module_##_name.level >= PACKET)                               \
        __log_func(PACKET, __log_module_##_name.name, ##__VA_ARGS__)

#define log_info_m(_name, ...)                                              \
    if (__log_module_##_name.level >= INFO)                                 \
        __log_func(INFO, __log_module_##_name.name, ##__VA_ARGS__)

#define log_warning_m(_name, ...)                                           \
    if (__log_module_##_name.level >= WARNING)                              \
        __log_func(WARNING, __log_module_##_name.name, ##__VA_ARGS__)

#define log_error_m(_name, ...)                                             \
    if (__log_module_##_name.level >= ERROR)                                \
        __log_func(ERROR, __log_module_##_name.name, ##__VA_ARGS__)

#define log_perror_m(_name, ...)                                            \
    if (__log_module_##_name.level >= ERROR)                                \
        __log_perror_func(__log_module_##_name.name, ##__VA_ARGS__)


#define log_debug(...)                                                      \
    if (__default_log_module->level >= DEBUG)                               \
        __log_func(DEBUG, __default_log_module->name, ##__VA_ARGS__)

#define log_packet(...)                                                     \
    if (__default_log_module->level >= PACKET)                              \
        __log_func(PACKET, __default_log_module->name, ##__VA_ARGS__)

#define log_info(...)                                                       \
    if (__default_log_module->level >= INFO)                                \
        __log_func(INFO, __default_log_module->name, ##__VA_ARGS__)

#define log_warning(...)                                                    \
    if (__default_log_module->level >= WARNING)                             \
        __log_func(WARNING, __default_log_module->name, ##__VA_ARGS__)

#define log_error(...)                                                      \
    if (__default_log_module->level >= ERROR)                               \
        __log_func(ERROR, __default_log_module->name, ##__VA_ARGS__)

#define log_pwarning(...)                                                   \
    if (__default_log_module->level >= WARNING)                             \
        __log_p_func(WARNING, __default_log_module->name, ##__VA_ARGS__)

#define log_perror(...)                                                     \
    if (__default_log_module->level >= ERROR)                               \
        __log_p_func(ERROR, __default_log_module->name, ##__VA_ARGS__)


#define log_enabled(_level)                                                 \
        (__default_log_module->level >= _level)

#define log_enabled_m(_name, _level)                                        \
        (__log_module_##_name.level >= _level)


#define log_warning_once(...)                                                   \
        if (__default_log_module->level >= WARNING) {                           \
            static int was_warn = 0;                                            \
            if (was_warn == 0) {                                                \
                __log_func(WARNING, __default_log_module->name, ##__VA_ARGS__); \
                was_warn = 1;                                                   \
            }                                                                   \
        }

typedef int log_getlevel_cb(const char *mod_name, LOGGING_LEVELS current_level, void *userdata);

// @userdata is supplied to the callback
// the callback only receives the current level for the first registered module with that name
// aborts the loop and returns 0 if @cb returns 0
// otherwise returns 1
int log_get_levels(log_getlevel_cb *cb, void *userdata);

// sets @new_level in all modules with @mod_name
// @returns false if no such module exists
bool log_set_level(const char *mod_name, LOGGING_LEVELS new_level);

/* Based on the config file's name and PID, this function
 * generate a unique logname, which can be used to:
 * 1. <logname>.log - logfile name prefix for file output
 * 2. <logname> - identifier for the r2dtwo instance in syslog
 *
 * @param[in] config file's name from argv[1]
 * @retval char * per-node unique identifier name "logname" for the process
 * */
char *logname_from_config(const char *config);

#endif // R2_LOG_H

// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_LOG_H
#define R2_LOG_H

#include <stdbool.h>

typedef enum {
    LOG_NONE=0,
    LOG_ERROR,
    LOG_WARNING,
    LOG_INFO,
    LOG_PACKET,
    LOG_DEBUG,
    LOG_ALL
} LOGGING_LEVELS;

/**
 * @brief Function to initialize the logger.
 *
 * @params[in] module Log module
 * @params[in] level Log level.
 * @params[in] filename Path to the log file.
 *
 * @retval true if successful.
 */
bool open_log(char *log_filename);

/* Close the log facility.
 * Closes the logfile
 * */
void close_log(void);

LOGGING_LEVELS log_level_from_string(const char *level);

const char *log_string_from_level(LOGGING_LEVELS level);

struct __log_module {
    const char *name;
    LOGGING_LEVELS level;
};

void __register_log_module(struct __log_module *mod);

int __log_func(LOGGING_LEVELS level, const char *logmodule, const char *frmt, ...)
    __attribute__((format(printf, 3, 4)))
    __attribute__((nonnull(3)));

// declare a module name and default level limit for logging
// the same module name can be declared in multiple compilation units
#define LOGGING_MODULE(_name, _default_level)                               \
static struct __log_module __log_module_##_name = {#_name, _default_level}; \
static void __attribute((constructor)) register_module_##_name(void) {      \
    __register_log_module(&__log_module_##_name);                           \
}

// declare a module for logging that can be used without supplying the name
// only one default can be declared in a compilation unit
#define DEFAULT_LOGGING_MODULE(_name, _default_level)                       \
static struct __log_module __log_module_##_name = {#_name, _default_level}; \
static struct __log_module *__default_log_module = &__log_module_##_name;   \
static void __attribute((constructor)) register_module_##_name(void) {      \
    __register_log_module(&__log_module_##_name);                           \
}

#define log_debug_m(_name, ...)                                             \
    if (__log_module_##_name.level >= LOG_DEBUG)                            \
        __log_func(LOG_DEBUG, __log_module_##_name.name, ##__VA_ARGS__)

#define log_packet_m(_name, ...)                                            \
    if (__log_module_##_name.level >= LOG_PACKET)                           \
        __log_func(LOG_PACKET, __log_module_##_name.name, ##__VA_ARGS__)

#define log_info_m(_name, ...)                                              \
    if (__log_module_##_name.level >= LOG_INFO)                             \
        __log_func(LOG_INFO, __log_module_##_name.name, ##__VA_ARGS__)

#define log_warning_m(_name, ...)                                           \
    if (__log_module_##_name.level >= LOG_WARNING)                          \
        __log_func(LOG_WARNING, __log_module_##_name.name, ##__VA_ARGS__)

#define log_error_m(_name, ...)                                             \
    if (__log_module_##_name.level >= LOG_ERROR)                            \
        __log_func(LOG_ERROR, __log_module_##_name.name, ##__VA_ARGS__)


#define log_debug(...)                                                      \
    if (__default_log_module->level >= LOG_DEBUG)                           \
        __log_func(LOG_DEBUG, __default_log_module->name, ##__VA_ARGS__)

#define log_packet(...)                                                     \
    if (__default_log_module->level >= LOG_PACKET)                          \
        __log_func(LOG_PACKET, __default_log_module->name, ##__VA_ARGS__)

#define log_info(...)                                                       \
    if (__default_log_module->level >= LOG_INFO)                            \
        __log_func(LOG_INFO, __default_log_module->name, ##__VA_ARGS__)

#define log_warning(...)                                                    \
    if (__default_log_module->level >= LOG_WARNING)                         \
        __log_func(LOG_WARNING, __default_log_module->name, ##__VA_ARGS__)

#define log_error(...)                                                      \
    if (__default_log_module->level >= LOG_ERROR)                           \
        __log_func(LOG_ERROR, __default_log_module->name, ##__VA_ARGS__)


typedef int log_modules_foreach_cb(const char *mod_name, LOGGING_LEVELS current_level, void *userdata);

// @userdata is supplied to the callback
// the callback only receives the current level for the first registered module with that name
// aborts the elimination and returns 0 if @cb returns 0
// otherwise returns 1
int log_foreach_modules(log_modules_foreach_cb *cb, void *userdata);

// sets @new_level in all modules with @mod_name
// @returns false if no such module exists
bool log_set_level(const char *mod_name, LOGGING_LEVELS new_level);

#endif // R2_LOG_H

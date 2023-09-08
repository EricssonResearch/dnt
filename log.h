// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_LOG_H
#define R2_LOG_H

#include <stdio.h>
#include <stdbool.h>

#define DEFAULT_LOG_PATH "/var/log/logger.log"
#define RESET 0

typedef enum {
    MAIN=0,
    CONFIG,
    PIPELINE,
    INTERFACE,
    OAM
} LOGGING_MODULE;

typedef enum {
    LOG_NONE=0,
    LOG_ERROR,
    LOG_DEBUG,
    LOG_WARNING,
    LOG_INFO,
    LOG_PACKET,
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
 bool init_log(unsigned short module_mask, int level, char *log_filename);

/**
 * @brief Function to write the message to the log file.
 *
 * @params[in] level Log level.
 * @params[in] module Log module.
 * @params[in] frmt Format message.
 * @params[in] ... Variable arguments.
 *
 * @retval None.
 */
void log_func(int level, LOGGING_MODULE module, const char *frmt, ...);

#define log_debug(...) log_func(LOG_DEBUG, __VA_ARGS__)
#define log_info(...)  log_func(LOG_INFO, __VA_ARGS__)
#define log_warn(...)  log_func(LOG_WARNING, __VA_ARGS__)
#define log_error(...) log_func(LOG_ERROR, __VA_ARGS__)
#define log_packet(...)  log_func(LOG_PACKET, __VA_ARGS__)

/* Close the log facility.
 * Closes the logfile
 * */
void close_log(void);

#endif // R2_LOG_H

// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "log.h"
#include "utils.h"
#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdbool.h>

// Global log variables
static FILE *logfile = NULL;
static int log_level = LOG_NONE;
static unsigned short log_module_mask = 0;

static const char* log_level_strings[] = {
    "NONE",
    "ERROR",
    "WARNING",
    "INFO",
    "PACKET",
    "DEBUG",
};

static const char* log_module_strings[] = {
    "MAIN",
    "CONFIG",
    "PIPELINE",
    "INTERFACE",
    "OAM",
    "DIAGNOSTIC"
};

const char* colors[] = {
    "\x1B[0m",
    "\x1B[34m",
    "\x1B[32m",
    "\x1B[33m",
    "\x1B[31m",
};
#define RESET 0

bool init_log(unsigned short module_mask, int level, char *log_filename)
{
    printf("Init Logging facility.\n");
    logfile = fopen(log_filename, "a");
    if(NULL == logfile) {
        fprintf(stderr, "%sError:%s File '%s' not existing or Permission denied\n", colors[LOG_ERROR], log_filename, colors[RESET]);
        return false;
    }
    fprintf(stderr, "%sInfo:%s File '%s' opened for logging.\n", colors[LOG_INFO], colors[RESET], log_filename);
    setbuf(logfile, NULL); // this is slower but more reliable
    log_module_mask = module_mask;
    log_level = level;

    return true;
}

int __log_func(int level, LOGGING_MODULE logmodule, const char *frmt, ...)
{
    if (level > log_level) return 0;
    if (((1<<logmodule) & log_module_mask) == 0) return 0;
    //printf("log mod %x %x\n", (1<<module), ((1<<module) & log_module_mask));
    time_t now;
    time(&now);
    struct tm now_tm;
    localtime_r(&now, &now_tm);
    char date[32];
    strftime(date, sizeof(date), "%Y.%m.%d %H:%M:%S", &now_tm);

    char msg[1024];
    va_list argp;
    va_start(argp, frmt);
    vsnprintf(msg, sizeof(msg), frmt, argp);
    va_end(argp);

    return fprintf(logfile, "%s [%s] [%s] %s\n", date, log_module_strings[logmodule], log_level_strings[level], msg);
}

void close_log(void)
{
    if(logfile != NULL){
        fclose(logfile);
        fprintf(stderr, "%sInfo:%s Logfile closed.\n", colors[LOG_INFO], colors[RESET]);
    }
}

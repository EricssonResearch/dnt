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
static FILE *logfile;
static int log_level = LOG_NONE;
static unsigned short log_module_mask = 0;

static const char* log_level_strings[] = {
    "NONE",
    "[ERROR]",
    "[WARNING]",
    "[INFO]",
    "[PACKET]",
    "[DEBUG]",
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
    log_module_mask = module_mask;
    log_level = level;

    return true;
}

void log_func(int level, LOGGING_MODULE module, const char *frmt, ...)
{
    if(level > log_level) return;
    if(((1<<module) & log_module_mask) == 0) return;
    //printf("log mod %x %x\n", (1<<module), ((1<<module) & log_module_mask));
    time_t now;
    time(&now);
    char * date =ctime(&now);
    date[strlen(date) - 1] = '\0';
    fprintf(logfile,"%s [%s] %s ", date, log_module_strings[module], log_level_strings[level]);

    char *format = strdup_printf("%s\n", frmt);
    va_list argp;
    va_start(argp, frmt);
    vfprintf(logfile, format, argp);
    va_end(argp);
}

void close_log(void)
{
    if(logfile != NULL){
        fclose(logfile);
        fprintf(stderr, "%sInfo:%s Logfile closed.\n", colors[LOG_INFO], colors[RESET]);
    }
}

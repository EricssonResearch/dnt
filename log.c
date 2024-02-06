// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.

#define _GNU_SOURCE /* for str[n]dupa */

#include "log.h"
#include "hashmap.h"
#include "utils.h"

#include <errno.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/syslog.h>
#include <time.h>
#include <unistd.h>

static FILE *logfile = NULL;
static LOG_OUTPUT log_output = LOG_OUT_STDOUT;
static bool color = false;

static void __attribute__((constructor)) init_logfile(void)
{
    // this is a sane default, so we don't require open_log()
    // it seems stdout is not constant, so we can't assign it directly to the static variable
    logfile = stdout;
}

static const char* log_level_strings[] = {
    "NONE",
    "ERROR",
    "WARNING",
    "INFO",
    "PACKET",
    "DEBUG",
    "ALL",
};

const char* colors[] = {
    "\033[0m",
    "\033[1;31m",
    "\033[0;31m",
    "\033[0;33m",
    "\033[0;32m",
    "\033[0;34m",
};
#define RESET 0


char *logname_from_config(const char *config_name)
{
    pid_t pid = getpid();
    const char *start = config_name;
    for (const char *c=config_name; *c; c++) {
        if (*c == '/') start = ++c;
    }
    const char *end = NULL;
    for (const char *c=start; *c; c++) {
        if (*c == '.') end = c;
    }
    const char *confname = end ? strndupa(start, end-start) : start;
    return strdup_printf("r2dtwo-%s-%u.log", confname, pid);
}

bool open_log(LOG_OUTPUT out, char *logname)
{
    if (out == LOG_OUT_LOGFILE) {
        logfile = fopen(logname, "a");
        if(NULL == logfile) {
            fprintf(stderr, "%sError:%s could not open log file '%s': %s\n",
                    colors[ERROR], logname, colors[RESET], strerror(errno));
            return false;
        }
        color = false;
        printf("%sInfo:%s File '%s' opened for logging.\n", colors[INFO], colors[RESET], logname);
        setbuf(logfile, NULL); // this is slower but more reliable
        fprintf(logfile, "File '%s' opened for logging.\n", logname);
    } else if (out == LOG_OUT_STDOUT) {
        logfile = stdout;
        color = isatty(STDOUT_FILENO);
        printf("%sInfo:%s Logging to standard output.\n", colors[INFO], colors[RESET]);
    } else if (out == LOG_OUT_STDERR) {
        logfile = stderr;
        color = isatty(STDERR_FILENO);
        printf("%sInfo:%s Logging to standard error.\n", colors[INFO], colors[RESET]);
    } else if (out == LOG_OUT_SYSLOG) {
        printf("%sInfo:%s Logging to syslog.\n", colors[INFO], colors[RESET]);
        color = false;
        openlog(strdup(logname), 0, LOG_USER);
    } else {
        return false;
    }
    log_output = out;
    return true;
}

void close_log(void)
{
    if (logfile != NULL && logfile != stderr && logfile != stdout) {
        fclose(logfile);
        printf("%sInfo:%s Logfile closed.\n", colors[INFO], colors[RESET]);
    }
}

LOGGING_LEVELS log_level_from_string(const char *level)
{
    for (unsigned i=0; i<ARRAY_SIZE(log_level_strings); i++) {
        if (strcmp(level, log_level_strings[i]) == 0) return i;
    }
    return -1;
}

const char *log_string_from_level(LOGGING_LEVELS level)
{
    if (level >= 0 && level < ARRAY_SIZE(log_level_strings))
        return log_level_strings[level];
    return NULL;
}

static int log_level_to_syslog_level(LOGGING_LEVELS level)
{
    switch (level) {
        case NONE: return LOG_CRIT;
        case ERROR: return LOG_ERR;
        case WARNING: return LOG_WARNING;
        case INFO: return LOG_NOTICE;
        case PACKET: return LOG_INFO;
        case DEBUG: return LOG_DEBUG;
        case ALL: return LOG_DEBUG;
        default: return LOG_WARNING;
    }
}


struct ModuleInstance {
    struct __log_module *mod;
    const char *file;
    struct ModuleInstance *next;
};

static struct HashMap *mod_instances = NULL;

static int log_module_delete_cb(const char *key, void *value, void *userdata)
{
    (void)key;
    (void)userdata;
    struct ModuleInstance *v = value;
    while (v) {
        struct ModuleInstance *d = v;
        v = v->next;
        free(d);
    }
    return 1;
}

void __register_log_module(const char *file, struct __log_module *mod)
{
    if (mod_instances == NULL) {
        mod_instances = new_hashmap(11, log_module_delete_cb, NULL);
    }

    struct ModuleInstance *n = calloc_struct(ModuleInstance);
    n->mod = mod;
    n->file = file;

    struct ModuleInstance *inst = hashmap_find(mod_instances, mod->name);
    if (inst) {
        if (mod->level != inst->mod->level) {
            fprintf(stderr, "Inconsistent log level for module %s: %s %s vs. %s %s\n", mod->name,
                    file, log_string_from_level(mod->level),
                    inst->file, log_string_from_level(inst->mod->level));
        }
        n->next = inst->next;
        inst->next = n;
    } else {
        hashmap_insert(mod_instances, (char*)mod->name, n);
    }
}

static void __attribute__((destructor)) cleanup_mod_instances(void)
{
    delete_hashmap(mod_instances);
}

int __log_func(LOGGING_LEVELS level, const char *logmodule, const char *frmt, ...)
{
    char msg[1024];
    va_list argp;
    va_start(argp, frmt);
    vsnprintf(msg, sizeof(msg), frmt, argp);
    va_end(argp);

    if (log_output != LOG_OUT_SYSLOG) {
        time_t now;
        time(&now);
        struct tm now_tm;
        localtime_r(&now, &now_tm);
        char date[32];
        strftime(date, sizeof(date), "%Y.%m.%d %H:%M:%S", &now_tm);

        if (color) {
            return fprintf(logfile, "%s [\033[1m%s\033[0m] [%s%s%s] %s\n",
                    date, logmodule, colors[level], log_level_strings[level], colors[RESET], msg);
        } else {
            return fprintf(logfile, "%s [%s] [%s] %s\n",
                    date, logmodule, log_level_strings[level], msg);
        }
    } else {
        int syslog_prio = log_level_to_syslog_level(level);
        syslog(syslog_prio, "[%s] %s", logmodule, msg);
        return 0;
    }
}

int __log_perror_func(const char *logmodule, const char *frmt, ...)
{
    char msg[1024];
    va_list argp;
    va_start(argp, frmt);
    vsnprintf(msg, sizeof(msg), frmt, argp);
    va_end(argp);

    if (log_output != LOG_OUT_SYSLOG) {
        time_t now;
        time(&now);
        struct tm now_tm;
        localtime_r(&now, &now_tm);
        char date[32];
        strftime(date, sizeof(date), "%Y.%m.%d %H:%M:%S", &now_tm);

        if (color) {
            return fprintf(logfile, "%s [\033[1m%s\033[0m] [%s%s%s] %s: %s\n",
                    date, logmodule, colors[ERROR], log_level_strings[ERROR], colors[RESET], msg,
                    strerror(errno));
        } else {
            return fprintf(logfile, "%s [%s] [%s] %s: %s\n",
                    date, logmodule, log_level_strings[ERROR], msg,
                    strerror(errno));
        }
    } else {
        int syslog_prio = log_level_to_syslog_level(ERROR);
        syslog(syslog_prio, "[%s] %s: %s", logmodule, msg, strerror(errno));
        return 0;
    }
}

struct ModuleForeachState {
    log_getlevel_cb *cb;
    void *userdata;
};

static int mod_foreach_cb(const char *key, void *value, void *userdata)
{
    struct ModuleForeachState *ms = userdata;
    struct ModuleInstance *inst = value;
    if (ms->cb(key, inst->mod->level, ms->userdata) == 0) {
        return 0;
    } else {
        return 1;
    }
}

int log_get_levels(log_getlevel_cb *cb, void *userdata)
{
    if (cb == NULL) return 1;
    struct ModuleForeachState ms = {cb, userdata};
    return hashmap_foreach(mod_instances, mod_foreach_cb, &ms);
}

static int mod_setlevel_all_cb(const char *key, void *value, void *userdata)
{
    (void)key;
    LOGGING_LEVELS *new_level = userdata;
    struct ModuleInstance *inst = value;
    while (inst) {
        inst->mod->level = *new_level;
        inst = inst->next;
    }
    return 1;
}

bool log_set_level(const char *mod_name, LOGGING_LEVELS new_level)
{
    if (new_level > ALL) new_level = ALL;

    if (strcmp(mod_name, "ALL") == 0) {
        hashmap_foreach(mod_instances, mod_setlevel_all_cb, &new_level);
        return true;
    }

    struct ModuleInstance *inst = hashmap_find(mod_instances, mod_name);
    if (inst == NULL) return false;
    while (inst) {
        inst->mod->level = new_level;
        inst = inst->next;
    }
    return true;
}

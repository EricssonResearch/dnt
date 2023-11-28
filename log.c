// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.

#define _GNU_SOURCE //for str[n]dupa

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
static OUTPUT log_output = SYSLOG;

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
    "\x1B[0m",
    "\x1B[34m",
    "\x1B[32m",
    "\x1B[33m",
    "\x1B[31m",
};
#define RESET 0

char *logname_from_config(const char *config_name)
{
    char *logname = calloc(1, PATH_MAX);
    pid_t pid = getpid();
    const char *start = config_name;
    for (const char *c=config_name; *c; c++) {
        if (*c == '/') start = ++c;
    }
    const char *end = NULL;
    for (const char *c=start; *c; c++) {
        if (*c == '.') end = c;
    }
    char *confname = end ? strndupa(start, end-start) : strdupa(start);
    sprintf(logname, "r2dtwo-%s-%u", confname, pid);
    return logname;
}

bool open_log(int level, OUTPUT out, char *logname)
{
    if (level != -1) {
        //TODO: proper per-module verbosity from CLI
        if (log_set_level("ALL", level) == false) {
            fprintf(stderr, "Failed to set loglevel '%s'\n", log_string_from_level(level));
            return false;
        }
    }
    if (out == LOGFILE) {
        log_output = LOGFILE;
        logfile = fopen(strcat(logname, ".log"), "a");
        if(NULL == logfile) {
            fprintf(stderr, "%sError:%s could not open log file '%s': %s\n",
                    colors[ERROR], logname, colors[RESET], strerror(errno));
            return false;
        }
        fprintf(stderr, "%sInfo:%s File '%s' opened for logging.\n", colors[INFO], colors[RESET], logname);
        setbuf(logfile, NULL); // this is slower but more reliable
        return true;
    } else if (out == STDOUT) {
        logfile = stderr;
        log_output = STDOUT;
        fprintf(stderr, "%sInfo:%s Logging to standard output.\n", colors[INFO], colors[RESET]);
        return true;
    } else if (out == SYSLOG) {
        log_output = SYSLOG;
        fprintf(stderr, "%sInfo:%s Logging to syslog.\n", colors[INFO], colors[RESET]);
        openlog(strdup(logname), 0, LOG_USER);
        return true;
    }
    return false;
}

void close_log(void)
{
    if (logfile != NULL && logfile != stderr) {
        fclose(logfile);
        fprintf(stderr, "%sInfo:%s Logfile closed.\n", colors[INFO], colors[RESET]);
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

void __register_log_module(struct __log_module *mod)
{
    if (mod_instances == NULL) {
        mod_instances = new_hashmap(11, log_module_delete_cb, NULL);
    }

    struct ModuleInstance *n = calloc_struct(ModuleInstance);
    n->mod = mod;

    struct ModuleInstance *inst = hashmap_find(mod_instances, mod->name);
    if (inst) {
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

    if (log_output != SYSLOG) {
        time_t now;
        time(&now);
        struct tm now_tm;
        localtime_r(&now, &now_tm);
        char date[32];
        strftime(date, sizeof(date), "%Y.%m.%d %H:%M:%S", &now_tm);

        return fprintf(logfile, "%s [%s] [%s] %s\n", date, logmodule, log_level_strings[level], msg);
    } else {
        int syslog_prio = log_level_to_syslog_level(level);
        syslog(syslog_prio, "[%s] %s", logmodule, msg);
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

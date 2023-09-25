// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "log.h"
#include "hashmap.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>

static FILE *logfile = NULL;

static const char* log_level_strings[] = {
    "NONE",
    "ERROR",
    "WARNING",
    "INFO",
    "PACKET",
    "DEBUG",
};

const char* colors[] = {
    "\x1B[0m",
    "\x1B[34m",
    "\x1B[32m",
    "\x1B[33m",
    "\x1B[31m",
};
#define RESET 0

bool open_log(char *log_filename)
{
    logfile = fopen(log_filename, "a");
    if(NULL == logfile) {
        fprintf(stderr, "%sError:%s could not open log file '%s': %s\n",
                colors[LOG_ERROR], log_filename, colors[RESET], strerror(errno));
        return false;
    }
    fprintf(stderr, "%sInfo:%s File '%s' opened for logging.\n", colors[LOG_INFO], colors[RESET], log_filename);
    setbuf(logfile, NULL); // this is slower but more reliable

    return true;
}

void close_log(void)
{
    if (logfile != NULL) {
        fclose(logfile);
        fprintf(stderr, "%sInfo:%s Logfile closed.\n", colors[LOG_INFO], colors[RESET]);
    }
}

LOGGING_LEVELS log_level_from_string(const char *level)
{
    for (unsigned i=0; i<ARRAY_SIZE(log_level_strings); i++) {
        if (strcmp(level, log_level_strings[i]) == 0) return i;
    }
    return 0;
}

const char *log_string_from_level(LOGGING_LEVELS level)
{
    if (level >= 0 && level < ARRAY_SIZE(log_level_strings))
        return log_level_strings[level];
    return log_level_strings[0];
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

    return fprintf(logfile, "%s [%s] [%s] %s\n", date, logmodule, log_level_strings[level], msg);
}

struct ModuleForeachState {
    log_modules_foreach_cb *cb;
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

int log_foreach_modules(log_modules_foreach_cb *cb, void *userdata)
{
    if (cb == NULL) return 1;
    struct ModuleForeachState ms = {cb, userdata};
    return hashmap_foreach(mod_instances, mod_foreach_cb, &ms);
}

bool log_set_level(const char *mod_name, LOGGING_LEVELS new_level)
{
    struct ModuleInstance *inst = hashmap_find(mod_instances, mod_name);
    if (inst == NULL) return false;
    while (inst) {
        inst->mod->level = new_level;
        inst = inst->next;
    }
    return true;
}

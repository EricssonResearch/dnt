// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.

#include "configfile.h"
#include "delay.h"
#include "hashmap.h"
#include "interface.h"
#include "log.h"
#include "notification.h"
#include "oam.h"
#include "seq_gen.h"
#include "sysmon.h"
#include "version.h"

#include <argp.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <signal.h>

DEFAULT_LOGGING_MODULE(MAIN, INFO);

int sigint_count = 0;

static void sigint_handler(int sig, siginfo_t *si, void *uc)
{
    (void)si;
    (void)uc;

    log_info("%s signal caught", strsignal(sig));
    sigint_count++;
}

static void sigusr1_handler(int sig, siginfo_t *si, void *uc)
{
    (void)sig;
    (void)si;
    (void)uc;

    log_info("SIGUSR1 caught, resetting seq generators");
    state_foreach_objects(reset_seq_generator, NULL);
}

static void sigusr2_handler(int sig, siginfo_t *si, void *uc)
{
    (void)sig;
    (void)si;
    (void)uc;
}

static void main_loop(void)
{
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = sigint_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) < 0) {
        log_perror("sigaction for SIGINT");
        return;
    }
    if (sigaction(SIGTERM, &sa, NULL) < 0) {    // SIGTERM is received when terminated via kill
        log_perror("sigaction for SIGTERM");
        return;
    }

    sa.sa_sigaction = sigusr1_handler;
    if (sigaction(SIGUSR1, &sa, NULL) < 0) {
        log_perror("sigaction for SIGUSR1");
        return;
    }

    // must catch the SIGUSR2 of thread_wakeup()
    sa.sa_sigaction = sigusr2_handler;
    if (sigaction(SIGUSR2, &sa, NULL) < 0) {
        log_perror("sigaction for SIGUSR2");
        return;
    }

    while (sigint_count == 0) {
        sleep(2);
        packets_check_performance();
    }
}

static int add_if_to_close(struct Interface *iface, void *userdata)
{
    struct HashMap *del_ifaces = (struct HashMap *)userdata;
    hashmap_insert(del_ifaces, strdup(iface->name), NULL);
    return 1;
}

static void close_interfaces(void)
{
    struct StateTransaction *tr = new_transaction("shutdown");

    state_foreach_interfaces(add_if_to_close, tr->del_ifaces);

    bool commit_success = state_commit_transaction(tr);
    delete_transaction(tr);
    if (!commit_success) {
        log_error("failed to close the interfaces");
    }
}

// expected format: "MOD1:LEVEL1,MOD2:LEVEL2"
static bool set_loglevels(const char *levels)
{
#define THROW(msg, ...)                             \
    do {                                            \
        log_error(msg, ##__VA_ARGS__);              \
        free(s);                                    \
        return false;                               \
    } while (0)

    char *s = strdup(levels);
    char *p = s;

    while (1) {
        char *m = p;
        char *colon = strchr(m, ':');
        if (colon) {
            *colon = 0;
            char *l = colon + 1;
            char *comma = strchr(l, ',');
            if (comma) {
                *comma = 0;
            }

            if (!log_level_valid(l)) {
                THROW("Invalid log level '%s'", l);
            }
            enum LoggingLevel nlvl = log_level_from_string(l);
            if (!log_set_level(m, nlvl)) {
                THROW("Module '%s' does not exist", m);
            }

            if (comma) {
                p = comma + 1;
            } else {
                free(s);
                return true;
            }
        } else {
            THROW("Missing log level");
        }
    }

    free(s);
    return true;
#undef THROW
}

static bool set_notiflevels(const char *levels)
{
#define THROW(msg, ...)                             \
    do {                                            \
        log_error(msg, ##__VA_ARGS__);              \
        free(s);                                    \
        return false;                               \
    } while (0)

    char *s = strdup(levels);
    char *p = s;

    while (1) {
        char *t = p;
        char *colon = strchr(t, ':');
        if (colon) {
            *colon = 0;
            char *l = colon + 1;
            char *comma = strchr(l, ',');
            if (comma) {
                *comma = 0;
            }

            if (!notification_level_valid(l)) {
                THROW("Invalid notification level '%s'", l);
            }
            NotificationLevel nlvl = notification_level_from_string(l);

            if (strcmp(t, "LOG") == 0) {
                notification_set_log_level(nlvl);
            } else if (strcmp(t, "SUBMIT") == 0) {
                notification_set_submit_level(nlvl);
            } else {
                THROW("Invalid notification target '%s'", t);
            }

            if (comma) {
                p = comma + 1;
            } else {
                free(s);
                return true;
            }
        } else {
            THROW("Missing notification level");
        }
    }

    free(s);
    return true;
#undef THROW
}

static char args_doc[] = "CONFIGFILE";

static struct argp_option options[] = {
    {"hostname", 'h', "name", 0, "Override hostname (for notifications)", 0},
    {"notify", 'n', "{LOG|SUBMIT}:LEVEL", 0, "Available levels: NONE, ERROR, WARNING, INFO, PULL, ALL", 0},
    {"output", 'o', "logfile", 0, "Output: log[f]ile, sys[l]og, [s]tdout (default), std[e]rr", 0},
    {"verbose", 'v', "MODULE:LEVEL", 0, "Available levels: NONE, ERROR, WARNING, INFO, PACKET, DEBUG, ALL", 0},
    { 0, 0, 0, 0, 0, 0 }
};

static struct arguments {
    char *configfile;
    char *hostname;
    char *notification;
    char *verbosity;
    enum LoggingOutput output;
} arguments;

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct arguments *args = (struct arguments *)state->input;
    if (state->arg_num > 1)
        argp_error(state, "Too many arguments");
    switch (key) {
        case 'h':
            args->hostname = arg;
            break;
        case 'n':
            args->notification = arg;
            break;
        case 'o':
            if (strlen(arg) == 1) {
                switch (arg[0]) {
                    case 'f': args->output = LOG_OUT_LOGFILE; break;
                    case 'l': args->output = LOG_OUT_SYSLOG; break;
                    case 's': args->output = LOG_OUT_STDOUT; break;
                    case 'e': args->output = LOG_OUT_STDERR; break;
                    default:
                        argp_error(state, "Invalid value '%c' for output argument.", arg[0]);
                }
            } else {
                if (strcmp(arg, "logfile") == 0) args->output = LOG_OUT_LOGFILE;
                else if (strcmp(arg, "syslog") == 0) args->output = LOG_OUT_SYSLOG;
                else if (strcmp(arg, "stdout") == 0) args->output = LOG_OUT_STDOUT;
                else if (strcmp(arg, "stderr") == 0) args->output = LOG_OUT_STDERR;
                else argp_error(state, "Invalid value '%s' for output argument.", arg);
            }
            break;
        case 'v':
            args->verbosity = arg;
            break;
        case ARGP_KEY_ARG:
            args->configfile = arg;
            break;
        case ARGP_KEY_END:
            if (state->arg_num != 1) {
                argp_state_help(state, state->out_stream, ARGP_HELP_LONG | ARGP_HELP_SHORT_USAGE);
                argp_error(state, "Config file required!");
            }
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct argp argp = { options, parse_opt, args_doc, NULL, NULL, NULL, NULL };

static void init_rand(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    srand(ts.tv_nsec);
}

int main(int argc, char **argv)
{
    init_rand();

    const char *verbose_env = getenv("R2DTWO_VERBOSE");
    if (verbose_env) {
        if (!set_loglevels(verbose_env)) {
            log_error("Verbosity environment '%s' is invalid", verbose_env);
            return EXIT_FAILURE;
        }
    }

    arguments.output = LOG_OUT_STDOUT;
    arguments.verbosity = NULL;
    arguments.configfile = NULL;
    argp_parse(&argp, argc, argv, 0, NULL, &arguments);

    char *logname = logname_from_config(argv[0], arguments.configfile);
    if (!open_log(arguments.output, logname)) {
        log_error("Failed to open the log.");
        free(logname);
        return EXIT_FAILURE;
    }
    free(logname);

    if (arguments.verbosity) {
        if (!set_loglevels(arguments.verbosity)) {
            log_error("Verbosity argument '%s' is invalid", arguments.verbosity);
            return EXIT_FAILURE;
        }
    }

    if (arguments.notification) {
        if (!set_notiflevels(arguments.notification)) {
            log_error("Notification argument '%s' is invalid", arguments.notification);
            return EXIT_FAILURE;
        }
    }

    if (arguments.hostname) {
        notification_override_hostname(arguments.hostname);
    }

    log_info("R2DTWO - Reliable & Robust Deterministic Tool for netWOrking %d.%d", VERSION_MAJOR, VERSION_MINOR);

    log_info("Reading config '%s'", arguments.configfile);
    struct StateTransaction *tr = read_config_file(arguments.configfile);
    if (tr == NULL) {
        log_error("the config is invalid");
        return EXIT_FAILURE;
    }

    if (!init_notification(tr->streams)) {
        log_error("failed to start the notification system");
        delete_transaction(tr);
        return EXIT_FAILURE;
    }
    if (!init_oam(arguments.hostname)) {
        log_error("failed to start the oam module");
        delete_transaction(tr);
        finish_notification();
        return EXIT_FAILURE;
    }
    if (!init_delay()) {
        log_error("failed to start the delay module");
        delete_transaction(tr);
        finish_notification();
        finish_oam();
        return EXIT_FAILURE;
    }

    bool commit_success = state_commit_transaction(tr);
    delete_transaction(tr);
    if (!commit_success) {
        log_error("failed to apply the config");
        finish_notification();
        finish_oam();
        finish_delay();
        return EXIT_FAILURE;
    }

    init_monitor();

    struct JsonValue *msg = json_object();
    json_object_insert(msg, "status", json_string("startup completed"));
    notification_push_event("r2dtwo", NOTIF_INFO, msg);

    main_loop();
    log_info("receive loop ended");

    close_interfaces();
    finish_delay();
    finish_oam();
    finish_monitor();
    finish_notification();
    close_log();

    return EXIT_SUCCESS;
}

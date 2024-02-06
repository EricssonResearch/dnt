// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.

#include "configfile.h"
#include "delay.h"
#include "hashmap.h"
#include "interface.h"
#include "if_eth.h"
#include "packet.h"
#include "parsetree.h"
#include "pipeline.h"
#include "protocol.h"
#include "seq_gen.h"
#include "time_utils.h"
#include "oam.h"
#include "log.h"
#include "version.h"

#include <argp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <limits.h>

#include <signal.h>
#include <sys/epoll.h>

#define MAX_EVENTS 10

DEFAULT_LOGGING_MODULE(MAIN, INFO);

int sigint_count = 0;
struct R2d2Config *config;

static void sigint_handler(int sig, siginfo_t *si, void *uc)
{
    (void)si;
    (void)uc;

    printf("%s signal caught\n", strsignal(sig));
    sigint_count++;
}

static void sigusr1_handler(int sig, siginfo_t *si, void *uc)
{
    (void)si;
    (void)uc;

    if (sig != SIGUSR1)
        return;
    printf("SIGUSR1 caught\n");

    hashmap_foreach(config->objects, reset_all_seq_generators, NULL);
}

static int add_iface_to_epollfd(const char *key, void *value, void *userdata) {
    struct Interface *iface = value;
    int *epollfd = userdata;
    if (iface->recvfd == 0) return 1;

    log_debug("adding interface %s to epoll", key);

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.ptr = iface;
    if (epoll_ctl(*epollfd, EPOLL_CTL_ADD, iface->recvfd, &ev) < 0) {
        log_perror("add interface %s to epoll", iface->name);
        return 0;
    }
    return 1;
}

static void recv_loop(struct HashMap *ifaces)
{
    struct sigaction sa;
    //struct sigevent sev;
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

    int epollfd = epoll_create1(0);
    if (epollfd < 0) {
        log_perror("epoll_create1 failed");
        return;
    }

    if (hashmap_foreach(ifaces, add_iface_to_epollfd, &epollfd) == 0) {
        return;
    }

    struct timespec last_perfcheck_time;
    clock_gettime(CLOCK_REALTIME, &last_perfcheck_time);
    struct epoll_event events[MAX_EVENTS];
    while (sigint_count == 0) {
        int nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            // a signal, keep receiving
            if (errno == EINTR) {
                continue;
            }
            log_perror("epoll_wait");
            return;
        }

        for (int n=0; n<nfds; n++) {
            struct Interface *recvif = events[n].data.ptr;
            struct Packet *p = recvif->recv(recvif);
            if (p == NULL)
                continue;
            //log_packet("received packet length %u on %s", p->len, recvif->name);
            packet_logcat(p, "%s %u ", recvif->name, p->len);
            struct Pipeline *pipe = parsetree_process(recvif->parsetree, p);
            if (pipe == NULL) {
                packet_print(p);
                delete_packet(p);
            } else {
                // the iterator owns the packet
                struct PipelineIterator *pi = new_pipe_iterator(pipe, p);
                // the iterator deletes itself when it's done
                pipe_iterator_run(pi);
            }
        }

        struct timespec now, diff;
        clock_gettime(CLOCK_REALTIME, &now);
        timespecsub(&now, &last_perfcheck_time, &diff);
        unsigned diff_msec;
        timespec_to_msec(diff_msec, &diff);
        if (diff_msec > 2000) {
            packets_check_performance();
            last_perfcheck_time = now;
        }
    }
}

static int open_interface(const char *key, void *value, void *userdata)
{
    (void)userdata;
    log_debug("opening interface %s", key);
    struct Interface *iface = value;
    return iface->open(iface);
}

// expected format: "MOD1:LEVEL1,MOD2:LEVEL2"
static bool set_loglevels(const char *levels)
{
#define THROW(msg, ...)                             \
    do {                                            \
        fprintf(stderr, msg "\n", ##__VA_ARGS__);   \
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
                //printf("module '%s' level '%s'\n", m, l);
                int nlvl = log_level_from_string(l);
                if (nlvl < 0) {
                    THROW("Log level '%s' invalid", l);
                }
                if (!log_set_level(m, nlvl)) {
                    THROW("Module '%s' does not exist", m);
                }
                p = comma + 1;
            } else {
                //printf("last module '%s' level '%s'\n", m, l);
                int nlvl = log_level_from_string(l);
                if (nlvl < 0) {
                    THROW("Log level '%s' invalid", l);
                }
                if (!log_set_level(m, nlvl)) {
                    THROW("Module '%s' does not exist", m);
                }
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

static char args_doc[] = "CONFIGFILE";

static struct argp_option options[] = {
    {"verbose", 'v', "MODULE:LEVEL", 0, "Available levels: NONE, ERROR, WARNING, INFO, PACKET, DEBUG, ALL", 0},
    {"output", 'o', "logfile", 0, "Output: log[f]ile, sys[l]og, [s]tdout (default), std[e]rr", 0},
    { 0 }
};

static struct arguments {
    char *configfile;
    char *verbosity;
    LOG_OUTPUT output;
} arguments;

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct arguments *args = state->input;
    if (state->arg_num > 1)
        argp_error(state, "Too many arguments");
    switch (key) {
        case 'v': {
            args->verbosity = arg;
        }
        break;
        case 'o': {
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
        }
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

int main(int argc, char **argv)
{
    printf("R2DTWO - Reliable & Robust Deterministic Tool for netWOrking\n"
            "Version %d.%d\n", VERSION_MAJOR, VERSION_MINOR);

    const char *verbose_env = getenv("R2DTWO_VERBOSE");
    if (verbose_env) {
        if (!set_loglevels(verbose_env)) {
            fprintf(stderr, "Verbosity string '%s' is invalid\n", verbose_env);
            return EXIT_FAILURE;
        }
    }

    arguments.output = LOG_OUT_STDOUT;
    arguments.verbosity = NULL;
    arguments.configfile = NULL;
    argp_parse(&argp, argc, argv, 0, NULL, &arguments);

    char *logname = logname_from_config(arguments.configfile);
    if (!open_log(arguments.output, logname)) {
        fprintf(stderr, "Failed to initialize the logging.\n");
        free(logname);
        return EXIT_FAILURE;
    }
    free(logname);

    if (arguments.verbosity) {
        if (!set_loglevels(arguments.verbosity)) {
            fprintf(stderr, "Verbosity string '%s' is invalid\n", arguments.verbosity);
            return EXIT_FAILURE;
        }
    }

    //TODO test log levels
    /*log_set_level("MAIN", ALL);
    log_error("error");
    log_warning("warning");
    log_info("info");
    log_packet("packet");
    log_debug("debug");*/

    printf("Reading config '%s'\n", arguments.configfile);
    config = read_config(arguments.configfile);
    if (config == NULL) {
        fprintf(stderr, "the config is invalid\n");
        return EXIT_FAILURE;
    }

    if (!config_add_streams_to_interfaces(config)) {
        delete_config(config);
        return EXIT_FAILURE;
    }

    if (hashmap_foreach(config->ifaces, open_interface, NULL) == 0) {
        delete_config(config);
        return EXIT_FAILURE;
    }

    if (!init_delay()) {
        delete_config(config);
        return EXIT_FAILURE;
    }

    if(!init_oam(config->oam)) {
        fprintf(stderr, "OAM init failed\n");
        delete_config(config);
        return EXIT_FAILURE;
    }

    recv_loop(config->ifaces);
    printf("receive loop ended\n");

    finish_oam();

    fini_delay();

    delete_config(config);

    close_log();

    return EXIT_SUCCESS;
}

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
LOGGING_MODULE(CONFIG, INFO);

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
            struct Pipeline *pipe = parsetree_process(recvif->parsetree, p);
            if (pipe == NULL) {
                log_packet("no pipeline found, unknown stream");
                delete_packet(p);
            } else {
                log_packet("parsetree identified %u headers, pipe %s", p->header_count, pipe->name);
                for (unsigned i=0; i<p->header_count; i++) {
                    log_packet("  header %u is %s at %u len %u", i,
                            protocol_list[p->headers[i].type].name, p->headers[i].start, p->headers[i].len);
                }
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

static char args_doc[] = "CONFIGFILE";

static struct argp_option options[] = {
    {"verbose", 'v', "DEFAULT", 0, "Available loglevels: NONE, ERROR, WARNING, INFO, PACKET, DEBUG, ALL", 0},
    {"output", 'o', "logfile", 0, "Output: log[f]ile, sys[l]og, [s]tdout", 0},
    { 0 }
};

static struct arguments {
    int verbosity;
    OUTPUT output;
    char *configfile;
} arguments;

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct arguments *args = state->input;
    if (state->arg_num > 1)
        argp_error(state, "Too many arguments");
    switch (key) {
        case 'v': {
            char level = log_level_from_string(arg);
            if (level != -1)
                args->verbosity = level;
            else {
                argp_error(state, "Invalid verbosity level '%s'", arg);
            }
        }
        break;
        case 'o': {
            if (strlen(arg) == 1) {
                switch (arg[0]) {
                    case 'f': args->output = LOGFILE; break;
                    case 'l': args->output = SYSLOG; break;
                    case 's': args->output = STDOUT; break;
                    default:
                        argp_error(state, "Invalid value '%c' for output argument.", arg[0]);
                }
            } else {
                if (strncmp(arg, "logfile", 7) == 0) args->output = LOGFILE;
                else if (strncmp(arg, "syslog", 6) == 0) args->output = SYSLOG;
                else if (strncmp(arg, "stdout", 6) == 0) args->output = STDOUT;
                else argp_error(state, "Invalid value '%s' for output argument.", arg);
            }
        }
        break;
        case ARGP_KEY_ARG:
            args->configfile = arg;
        break;
        case ARGP_KEY_END:
            if (state->arg_num != 1)
                argp_error(state, "Config file required!");
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

    arguments.output = SYSLOG;
    arguments.verbosity = -1;
    arguments.configfile = NULL;
    argp_parse(&argp, argc, argv, 0, NULL, &arguments);

    char *logname = logname_from_config(arguments.configfile);
    if (!open_log(arguments.output, logname)) {
        fprintf(stderr, "Failed to initialize the logging.\n");
        free(logname);
        return EXIT_FAILURE;
    }
    free(logname);
    if (arguments.verbosity >= 0) {
        log_set_level("ALL", arguments.verbosity); //TODO proper per-module verbosity from CLI
    }

    log_info_m(CONFIG, "Reading config '%s'", arguments.configfile);
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

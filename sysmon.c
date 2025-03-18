// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifdef _GNU_SOURCE /* stupid g++ implicitly defines this */
#undef _GNU_SOURCE /* we want the standard version of strerror_r */
#endif

#include "log.h"
#include "state.h"
#include "hashmap.h"
#include "sysmon.h"
#include "thread_utils.h"
#include "utils.h"
#include "interface.h"

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pty.h>
#include <poll.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/timerfd.h>
#include <sys/eventfd.h>

DEFAULT_LOGGING_MODULE(SYSMON, INFO);

#define MAX_LINE            1024
#define MAX_ID_LEN          31
#define SUBSCRIBE_CMD       "SET SUBSCRIBE_EVENTS_NP duration 100 NOTIFY_PORT_STATE on NOTIFY_TIME_SYNC on\n"
#define SUBSCRIBE_TIMEOUT   100

static pid_t pmc_pid;
static pthread_t pmc_monitor_thread;
int efd;  // Event file descriptor

static struct HashMap *subs = NULL;

static void *pmc_monitor(void *arg)
{
    int fd = *((int *)arg); // Dereferencing integer pointer
    char managementId[MAX_ID_LEN+1], portState[MAX_ID_LEN+1], portIdentity[MAX_ID_LEN+1];
    char buf[MAX_LINE+1], *st;
    int len;
    struct itimerspec  new_value;
    uint64_t           exp;

    FILE *stream = fdopen(fd, "r");  // Convert fd to FILE*
    if (!stream) {
        log_perror("fdopen %s", strerror(errno));
        return NULL;
    }

    new_value.it_value.tv_sec = SUBSCRIBE_TIMEOUT-1;
    new_value.it_value.tv_nsec = 500000000;
    new_value.it_interval.tv_sec = SUBSCRIBE_TIMEOUT-1;
    new_value.it_interval.tv_nsec = 500000000;

    int tfd = timerfd_create(CLOCK_REALTIME, 0);
    if (tfd == -1)
        log_perror("timerfd_create %s", strerror(errno));

    if (timerfd_settime(tfd, 0, &new_value, NULL) == -1)
        log_perror("timerfd_settime %s", strerror(errno));

    // Initial subscribe
    if (write(fd, SUBSCRIBE_CMD, sizeof(SUBSCRIBE_CMD)-1) < 0)      // -1 needed to exclude the string terminating 0
        log_perror("Failed to send to PMC: %s", strerror(errno));

    struct pollfd fds[3];
    fds[0].fd = fd;
    fds[0].events = POLLIN;
    fds[1].fd = tfd;
    fds[1].events = POLLIN;
    fds[2].fd = efd;
    fds[2].events = POLLIN;

    while(1){
        int ret = poll(fds, 3, -1); // block until something happens
        if (ret == -1) {
            log_perror("poll error: %s\n", strerror(errno));
            break;
        }

        if (fds[1].revents & POLLIN) {
            ssize_t s = read(tfd, &exp, sizeof(uint64_t));
            if (s == sizeof(uint64_t)) {
                // renew subscription
                if (write(fd, SUBSCRIBE_CMD, sizeof(SUBSCRIBE_CMD) - 1) < 0)
                    log_perror("Failed to send to PMC: %s\n", strerror(errno));
            }
            continue;
        }
        if (fds[0].revents & POLLIN) {
            if (fgets(buf, MAX_LINE, stream) != NULL) {
                if ((st = strstr(buf, "RESPONSE MANAGEMENT")) != NULL) {
                    st += 20;
                    len = MIN(strlen(st) - 2, MAX_ID_LEN);
                    strncpy(managementId, st, len);
                    managementId[len - 1] = '\0';
                }
                if ((st = strstr(buf, "portIdentity")) != NULL) {
                    st += 24;
                    len = MIN(strlen(st) - 1, MAX_ID_LEN);
                    strncpy(portIdentity, st, len);
                    portIdentity[len - 1] = '\0';
                }
                if ((st = strstr(buf, "portState")) != NULL) {
                    st += 24;
                    len = MIN(strlen(st) - 1, MAX_ID_LEN);
                    strncpy(portState, st, len);
                    portState[len - 1] = '\0';
                }

                if ((st = strstr(buf, "versionNumber")) != NULL) {
                    log_info("sync monitor: managementId: %s, portIdentity: %s, portState: %s\n", managementId, portIdentity, portState);
                    if ((strcmp(portState, "FAULTY") == 0) || (strcmp(portState, "UNCALIBRATED") == 0)) {
                        // Handle error notification logic
                    }
                }
            }
        }
        if (fds[2].revents & POLLIN) {      // eventfd
            uint64_t value;
            if(read(efd, &value, sizeof(value)) < 0)  // Clear the event
                    log_perror("Could not read eventfd.");
            break;  // Exit the loop and terminate the thread
        }
    }

    fclose(stream);  // Closes fd as well!
    close(tfd);
    close(fd);

    return NULL;
}

static NotificationLevel tc_stat_notification_pull_fn(void *self, struct JsonValue **msg)
{
    char *iface_name = (char *)self;
    char command[MAX_LINE],  buffer[MAX_LINE];

    snprintf(command, sizeof(command), "tc -s -j qdisc show dev %s handle 0", iface_name);
    FILE *fp = popen(command, "r");
    if (fp == NULL) {
        msg = NULL;
        return NOTIF_INFO;
    }

    struct JsonValue *ret;

    if (fgets(buffer, sizeof(buffer), fp) == 0){    // no -j (json) option, old tc
        ret = json_object();
        json_object_insert(ret, "error", json_string("tc does not support -j option"));
    }
    else {
        buffer[strlen(buffer)-2] = '\0';
        char *jerror;
        ret = json_parse(buffer+1, strlen(buffer), &jerror);
        if (ret == NULL || ret->type != JSON_OBJECT) {
            log_error("JSON in reply is invalid: %s", jerror);
            free(jerror);
            msg = NULL;
            return NOTIF_INFO;
        }
    }

    pclose(fp);

/*    unsigned n;
    char *jstr = json_serialize(ret, &n);
    printf("json: %s\n", jstr);
*/
    *msg = ret;
    return NOTIF_INFO;
}

static NotificationLevel modem_stat_notification_pull_fn(void *self, struct JsonValue **msg)
{
    char *iface_name = (char *)self;
    char command[MAX_LINE],  buffer[MAX_LINE];

    snprintf(command, sizeof(command), "echo AT |  socat - %s,crnl", iface_name);
    FILE *fp = popen(command, "r");
    if (fp == NULL) {
        msg = NULL;
        return NOTIF_INFO;
    }

    struct JsonValue *ret = json_object();

    do {
        printf("x: %s", buffer);
        // ToDo: implement modem commands needed...

    } while (fgets(buffer, sizeof(buffer), fp) != NULL);

    // Populate JSON
    json_object_insert(ret, "modem", json_string(buffer));

    pclose(fp);

/*    unsigned n;
    char *jstr = json_serialize(ret, &n);
    printf("json: %s\n", jstr);
*/
    *msg = ret;
    return NOTIF_INFO;
}

static int monitor_ptp(void)
{
    static int fd;

    pmc_pid = forkpty(&fd, 0, 0, 0);

    switch (pmc_pid) {  // pmc -u -b 0
    case 0: if (execlp("pmc", "pmc", "-u", "-b", "0", NULL) < 0) {
                log_perror("could not start pmc");
                _Exit(EXIT_FAILURE);
             }
             /* NOTREACHED */
             break;
    case -1: log_perror("could not forkpty ");
             _Exit(EXIT_FAILURE);
    default: break;
    }

    // Wait briefly to check if child process exits immediately due to execlp failure
    usleep(3000);  // Allow some time for failure detection (tunable)

    int status;
    pid_t result = waitpid(pmc_pid, &status, WNOHANG);
    if (result == pmc_pid) {
        // Child exited, meaning execlp likely failed
        pmc_pid = -1;  // Invalidate pmc_pid
        return EXIT_FAILURE;
    }

    // Create eventfd
    efd = eventfd(0, 0);
    if (efd < 0) {
        log_perror("eventfd failed");
        close(fd); // we only need to close the fd
        return EXIT_FAILURE;
    }

    // Create monitor thread
    if (pthread_create(&pmc_monitor_thread, NULL, pmc_monitor, &fd) != 0) {
        log_perror("could not create monitor thread");
        close(fd); // we only need to close the fd
        return EXIT_FAILURE;
    }

	return EXIT_SUCCESS;
}

bool register_tc_notification(bool add, char *target, unsigned period_ms)
{
    char notif_name[32];
    snprintf(notif_name, 32, "tc_%s", target);
    char *pname = strdup(target);

    if(add) {
        hashmap_insert(subs, strdup(notif_name), pname);
        return notification_register_source(notif_name, tc_stat_notification_pull_fn, pname, period_ms);
    } else {
        int ret = notification_register_source(notif_name, NULL, target, period_ms);
        if(ret)
            hashmap_remove(subs, notif_name);
        return ret;
    }
}

bool register_modem_notification(bool add, char *target, unsigned period_ms)
{
    char notif_name[32];
    snprintf(notif_name, 32, "modem_%s", target);
    char *pname = strdup(target);

    if(add) {
        hashmap_insert(subs, strdup(notif_name), pname);
        return notification_register_source(notif_name, modem_stat_notification_pull_fn, pname, period_ms);
    } else {
        int ret = notification_register_source(notif_name, NULL, target, period_ms);
        if(ret)
            hashmap_remove(subs, notif_name);
        return ret;
    }
}

static int check_if_notification(struct Interface *ifa, void *userdata) {
    (void) userdata;
    char command[MAX_LINE],  buffer[MAX_LINE];
    char kind[32] = {0};
    int ret = 0;

    if((ifa->type == IF_ETH) || (ifa->type == IF_IP) || (ifa->type == IF_UDP_OUT)) {
        // check if it's TAPRIO or MQPRIO
        snprintf(command, sizeof(command), "tc qdisc show dev %s root", ifa->ifname);
        FILE *fp = popen(command, "r");
        if (fp != NULL) {
            if (fgets(buffer, sizeof(buffer), fp) != NULL) {   // the first line is qdisc type
                if (strstr(buffer, "qdisc") != NULL) {
                    sscanf(buffer, "qdisc %s", kind);
                    if((strcmp(kind, "taprio")==0) || (strcmp(kind, "mqprio")==0)) {
                        register_tc_notification(true, ifa->ifname, 2000);
                        log_info("  Registered %s\n", ifa->ifname);
                        ret = 1;
                    }
                }
            }
            pclose(fp);
        }
    }
    return ret;
}

bool init_monitor(void)
{
    subs = new_hashmap(13, NULL, NULL);

    if (monitor_ptp() == EXIT_FAILURE)
        log_perror("PTP monitor not started. Is ptp4l (with pmc) installed?" );

    state_foreach_interfaces(check_if_notification, NULL);

    log_info("Monitor started");
    return true;
}

void finish_monitor(void)
{
    HASHMAP_ITERATE(subs, s) {
        notification_register_source((char *)hash_iterator_key(&s), NULL, (char *)hash_iterator_value(&s), 0);
    }
    delete_hashmap(subs);

    // Signal to pmc monitor to stop
    uint64_t signal_value = 1;
    if(write(efd, &signal_value, sizeof(signal_value)) < 0)
        log_error("Could not write eventfd.");
    // Wait for the thread to finish
    pthread_join(pmc_monitor_thread, NULL);
    // Cleanup efd
    close(efd);
    log_info("Monitor stopped.");
}

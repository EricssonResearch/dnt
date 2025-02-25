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

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/timerfd.h>

DEFAULT_LOGGING_MODULE(SYSMON, INFO);

#define MAX_LINE            1024
#define MAX_ID_LEN          31
#define SUBSCRIBE_CMD       "SET SUBSCRIBE_EVENTS_NP duration 100 NOTIFY_PORT_STATE on NOTIFY_TIME_SYNC on\n"
#define SUBSCRIBE_TIMEOUT   100

static pid_t pmc_pid;
struct Thread *pmc_monitor_thread;

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
        printf("fdopen");
        return NULL;
    }

    new_value.it_value.tv_sec = SUBSCRIBE_TIMEOUT-1;
    new_value.it_value.tv_nsec = 500000000;
    new_value.it_interval.tv_sec = SUBSCRIBE_TIMEOUT-1;
    new_value.it_interval.tv_nsec = 500000000;

    int tfd = timerfd_create(CLOCK_REALTIME, 0);
    if (tfd == -1)
        fprintf(stderr, "timerfd_create %s\n", strerror(errno));

    if (timerfd_settime(tfd, 0, &new_value, NULL) == -1)
        fprintf(stderr, "timerfd_settime %s\n", strerror(errno));

    // Initial subscribe
    if (write(fd, SUBSCRIBE_CMD, sizeof(SUBSCRIBE_CMD)-1) < 0)      // -1 needed to exclude the string terminating 0
        fprintf(stderr, "Failed to send to PMC: %s\n", strerror(errno));

    struct pollfd fds[2];
    fds[0].fd = fd;
    fds[0].events = POLLIN;
    fds[1].fd = tfd;
    fds[1].events = POLLIN;

    while(1){
        int ret = poll(fds, 2, 1000); // 1 seconds timeout

        if (ret == -1) {
            fprintf(stderr, "poll error: %s\n", strerror(errno));
            break;
        }
        if (fds[1].revents & POLLIN) {
            ssize_t s = read(tfd, &exp, sizeof(uint64_t));
            if (s == sizeof(uint64_t)) {
                // renew subscription
                if (write(fd, SUBSCRIBE_CMD, sizeof(SUBSCRIBE_CMD) - 1) < 0)
                    fprintf(stderr, "Failed to send to PMC: %s\n", strerror(errno));
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
                    printf("sync monitor: managementId: %s, portIdentity: %s, portState: %s\n", managementId, portIdentity, portState);
                    if ((strcmp(portState, "FAULTY") == 0) || (strcmp(portState, "UNCALIBRATED") == 0)) {
                        // Handle error notification logic
                    }
                }
            }
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

    char kind[32] = {0}, handle[16] = {0};
    unsigned long sent_bytes = 0, sent_packets = 0, dropped = 0, overlimits = 0, requeues = 0;
    unsigned long backlog_bytes = 0, backlog_packets = 0;
    unsigned long maxpacket = 0, drop_overlimit = 0, new_flow_count = 0, ecn_mark = 0;
    unsigned long new_flows_len = 0, old_flows_len = 0;
    int root = 0, refcnt = 0, limit = 0, flows = 0, quantum = 0, drop_batch = 0;
    unsigned long target = 0, interval = 0, memory_limit = 0;
    int ecn = 0;

    snprintf(command, sizeof(command), "tc -s -j qdisc show dev %s handle 0", iface_name);
    FILE *fp = popen(command, "r");
    if (fp == NULL) {
        msg = NULL;
        return NOTIF_INFO;
    }

    struct JsonValue *ret = json_object();

    if (fgets(buffer, sizeof(buffer), fp) == 0){    // no -j (json) option, old tc
        snprintf(command, sizeof(command), "tc -s qdisc show dev %s handle 0", iface_name);
        fp = popen(command, "r");
        if (fgets(buffer, sizeof(buffer), fp) == 0) {
            msg = NULL;
            return NOTIF_INFO;
        }

        do {
            //printf("x: %s", buffer);
            /* ToDo: handle multiple types of qdiscs, IF needed  */
            if (strstr(buffer, "qdisc") != NULL) {
                 sscanf(buffer, "qdisc %s %s root refcnt %d limit %d flows %d quantum %d target %lums interval %lums memory_limit %luMb ecn drop_batch %d",
                        kind, handle, &refcnt, &limit, &flows, &quantum, &target, &interval, &memory_limit, &drop_batch);
                 root = 1;
                 ecn = strstr(buffer, "ecn") ? 1 : 0;
             } else if (strstr(buffer, "Sent") != NULL) {
                 sscanf(buffer, "Sent %lu bytes %lu pkt (dropped %lu, overlimits %lu requeues %lu)",
                        &sent_bytes, &sent_packets, &dropped, &overlimits, &requeues);
             } else if (strstr(buffer, "backlog") != NULL) {
                 sscanf(buffer, "backlog %lub %lup requeues %lu", &backlog_bytes, &backlog_packets, &requeues);
             } else if (strstr(buffer, "maxpacket") != NULL) {
                 sscanf(buffer, " maxpacket %lu drop_overlimit %lu new_flow_count %lu ecn_mark %lu",
                        &maxpacket, &drop_overlimit, &new_flow_count, &ecn_mark);
             } else if (strstr(buffer, "new_flows_len") != NULL) {
                 sscanf(buffer, "  new_flows_len %lu old_flows_len %lu", &new_flows_len, &old_flows_len);
             }
        } while (fgets(buffer, sizeof(buffer), fp) != NULL);

        // Populate JSON
        json_object_insert(ret, "kind", json_string(kind));
        json_object_insert(ret, "handle", json_string(handle));
        json_object_insert(ret, "root", root?json_true():json_false());
        json_object_insert(ret, "refcnt", json_number(refcnt));

        // Options
        struct JsonValue *options = json_object();
        json_object_insert(options, "limit", json_number(limit));
        json_object_insert(options, "flows", json_number(flows));
        json_object_insert(options, "quantum", json_number(quantum));
        json_object_insert(options, "target", json_number(target * 1000 - 1));  // Convert ms to ns
        json_object_insert(options, "interval", json_number(interval * 1000 - 1)); // Convert ms to ns
        json_object_insert(options, "memory_limit", json_number(memory_limit * 1024 * 1024)); // Convert MB to Bytes
        json_object_insert(options, "ecn", ecn?json_true():json_false());
        json_object_insert(options, "drop_batch", json_number(drop_batch));
        json_object_insert(ret, "options", options);

        // Statistics
        json_object_insert(ret, "bytes", json_number(sent_bytes));
        json_object_insert(ret, "packets", json_number(sent_packets));
        json_object_insert(ret, "drops", json_number(dropped));
        json_object_insert(ret, "overlimits", json_number(overlimits));
        json_object_insert(ret, "requeues", json_number(requeues));
        json_object_insert(ret, "backlog", json_number(backlog_packets));
        json_object_insert(ret, "qlen", json_number(backlog_packets));

        // Additional Metrics
        json_object_insert(ret, "maxpacket", json_number(maxpacket));
        json_object_insert(ret, "drop_overlimit", json_number(drop_overlimit));
        json_object_insert(ret, "new_flow_count", json_number(new_flow_count));
        json_object_insert(ret, "ecn_mark", json_number(ecn_mark));
        json_object_insert(ret, "new_flows_len", json_number(new_flows_len));
        json_object_insert(ret, "old_flows_len", json_number(old_flows_len));
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
                exit(EXIT_FAILURE);
             }
             /* NOTREACHED */
             break;
    case -1: log_perror("could not forkpty ");
             exit(EXIT_FAILURE);
    default: break;
    }

    // Create monitor thread
    pmc_monitor_thread = thread_launch(pmc_monitor, &fd, "monitor");
    if (pmc_monitor_thread == NULL) {
        log_perror("could not create monitor thread");
        close(fd); // we only need to close the fd
        return EXIT_FAILURE;
    }

	return EXIT_SUCCESS;
}

bool register_tc_notification(bool add, char *target, unsigned period_ms)
{
    char notif_name[32];
    snprintf(notif_name, 32, "delay_%s", target);
    if(add)
        return notification_register_source(notif_name, tc_stat_notification_pull_fn, target, period_ms);
    else
        return notification_register_source(notif_name, NULL, target, period_ms);
}

bool register_modem_notification(bool add, char *target, unsigned period_ms)
{
    char notif_name[32];
    snprintf(notif_name, 32, "delay_%s", target);
    if(add)
        return notification_register_source(notif_name, modem_stat_notification_pull_fn, target, period_ms);
    else
        return notification_register_source(notif_name, NULL, target, period_ms);
}

bool init_monitor(struct HashMap *ifaces)
{
    if (monitor_ptp() == -1)
        return false;

    if(ifaces == NULL)
        log_perror("No interfaces?" );

    char command[MAX_LINE],  buffer[MAX_LINE];
    char kind[32] = {0};
    HASHMAP_ITERATE(ifaces, s) {
        struct Interface *ifa= (struct Interface *)hash_iterator_value(&s);
        if((ifa->type == IF_ETH) || (ifa->type == IF_IP) || (ifa->type == IF_UDP_OUT)) {
            // check if it's TAPRIO or MQPRIO
            snprintf(command, sizeof(command), "tc qdisc show dev %s root", ifa->ifname);
            FILE *fp = popen(command, "r");
            if (fp != NULL) {
                if (fgets(buffer, sizeof(buffer), fp) != NULL) {   // the first line is qdisc type
                    if (strstr(buffer, "qdisc") != NULL) {
                        sscanf(buffer, "qdisc %s", kind);
                        if(strcmp(kind, "taprio")==0) {
                            register_tc_notification(true, ifa->ifname, 2000);
                            log_info("  Registered %s\n", ifa->ifname);
                        }
                    }
                }
            }
            pclose(fp);
        }
    }

    log_info("Monitor started");
    return true;
}

void finish_monitor(void)
{
    log_info("Stopping monitor.");

    thread_stop(pmc_monitor_thread);
    //waitpid(pmc_pid, 0, 0);
    //pthread_join(pmc_monitor_thread, NULL);
}

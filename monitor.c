// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifdef _GNU_SOURCE /* stupid g++ implicitly defines this */
#undef _GNU_SOURCE /* we want the standard version of strerror_r */
#endif

#include "log.h"
#include "hashmap.h"
#include "monitor.h"
#include "thread_utils.h"
#include "interface.h"
#include "utils.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <pty.h>
#include <pthread.h>

#include <sys/wait.h>
#include <sys/types.h>


DEFAULT_LOGGING_MODULE(NOTIFICATION, INFO);


#define MAX_LINE        1024
#define MAX_ID_LEN      31
#define SUBSCRIBE_CMD   "SET SUBSCRIBE_EVENTS_NP duration 100 NOTIFY_PORT_STATE on NOTIFY_TIME_SYNC on\n"

static pid_t pmc_pid;
struct Thread *pmc_monitor_thread;

int init_monitor(void){
    log_info("monitor started");

    if (monitor_ptp() == -1)
        return -1;

    static char iface[] = "eno1";  // just for test
    notification_register_source(iface, tc_stat_notification_pull_fn, iface, 2000);

    return 0;
}

void *pmc_monitor(void *arg) {
    int fd = *((int *)arg); // Dereferencing integer pointer
    char managementId[MAX_ID_LEN+1], portState[MAX_ID_LEN+1], portIdentity[MAX_ID_LEN+1];
    char buf[MAX_LINE+1], *st;
    int len;
    fd_set fds;
    struct timeval tv;

    FILE *stream = fdopen(fd, "r");  // Convert fd to FILE*
    if (!stream) {
        printf("fdopen");
        return NULL;
    }

    if (write(fd, SUBSCRIBE_CMD, sizeof(SUBSCRIBE_CMD)-1) < 0)      // -1 needed to exclude the string terminating 0
        fprintf(stderr, "Failed to send to PMC: %s\n", strerror(errno));

    while(1){
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        tv.tv_sec = 99;
        tv.tv_usec = 500000;
        if(select(fd+1, &fds, NULL, NULL, &tv) == -1)
            fprintf(stderr, "select error: %s\n", strerror(errno));
        if (FD_ISSET(fd, &fds)) {
            if(fgets(buf, MAX_LINE, stream) != NULL){
                //printf("pmc: %s", buf);
                if ((st = strstr(buf, "RESPONSE MANAGEMENT")) != NULL) {
                    st += 20;  // Move to the data start position
                    len = MIN(strlen(st) - 2, MAX_ID_LEN);
                    strncpy(managementId, st, len);
                    managementId[len-1] = '\0';  // Ensure null termination
                }
                if ((st = strstr(buf, "portIdentity")) != NULL) {
                    st += 24;  // Move to the data start position
                    len = MIN(strlen(st) - 1, MAX_ID_LEN);
                    strncpy(portIdentity, st, len);
                    portIdentity[len-1] = '\0';  // Ensure null termination
                }
                if ((st = strstr(buf, "portState")) != NULL) {
                    st += 24;  // Move to the data start position
                    len = MIN(strlen(st) - 1, MAX_ID_LEN);
                    strncpy(portState, st, len);
                    portState[len-1] = '\0';  // Ensure null termination
                }

                if((st=strstr(buf, "versionNumber")) != NULL){      // last line, print out
                    log_error("sync monitor: managementId: %s, portIdentity: %s, portState: %s\n", managementId, portIdentity, portState);
                    if( (strcmp(portState,"FAULTY")==0) || (strcmp(portState,"UNCALIBRATED")==0)){
                        struct JsonValue *js = json_object();
                        json_object_insert(js, "error", json_string("sync error"));
                        json_object_insert(js, "portIdentity", json_string(portIdentity));
                        json_object_insert(js, "portState", json_string(portState));
                        notification_push_event("sync", NOTIF_ERROR, js);
                    }
                }
            }
        }

        // If timed out, send subscription again
        if(tv.tv_sec == 0)
            if (write(fd, SUBSCRIBE_CMD, sizeof(SUBSCRIBE_CMD)-1) < 0)
                fprintf(stderr, "Failed to send to PMC: %s\n", strerror(errno));
    }

    fclose(stream);  // Closes fd as well!
    close(fd);

    return NULL;
}

NotificationLevel tc_stat_notification_pull_fn(void *self, struct JsonValue **msg)
{
    char *iface_name = (char *)self;

    char command[MAX_LINE], *p;
    snprintf(command, sizeof(command), "tc -s qdisc show dev %s handle 0", iface_name);
    FILE *fp = popen(command, "r");
    if (fp == NULL) {
        msg = NULL;
        return NOTIF_INFO;
    }

    char buffer[MAX_LINE];
    unsigned long sent_packets = 0, sent_bytes = 0, dropped = 0, overlimits = 0, requeues = 0, backlog_bytes = 0,backlog_packets;

    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        if ((p=strstr(buffer, "Sent")) != NULL) {
            sscanf(p, "Sent %lu bytes %lu pkt (dropped %lu, overlimits %lu requeues %lu)", &sent_packets, &sent_bytes, &dropped, &overlimits, &requeues);
        } else if ((p=strstr(buffer, "backlog")) != NULL) {
            sscanf(p, "backlog %lub %lup", &backlog_bytes, &backlog_packets);
        }
    }

    pclose(fp);

    struct JsonValue *ret = json_object();
    json_object_insert(ret, "sent_packets", json_number(sent_packets));
    json_object_insert(ret, "dropped", json_number(dropped));
    json_object_insert(ret, "overlimits", json_number(overlimits));
    json_object_insert(ret, "backlog", json_number(backlog_packets));

    *msg = ret;
    return NOTIF_INFO;
}

int monitor_ptp(void){
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

void finish_monitor(void){
    log_info("Stopping monitor.");

    thread_stop(pmc_monitor_thread);
    //waitpid(pmc_pid, 0, 0);
    //pthread_join(pmc_monitor_thread, NULL);
}

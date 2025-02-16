// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.

#ifndef R2_MONITOR_H
#define R2_MONITOR_H

#include "log.h"
#include "json.h"
#include "notification.h"


#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <pthread.h>
#include <time.h>

#include "thread_utils.h"
#include "time_utils.h"
#include "utils.h"


int init_monitor(void);
void finish_monitor(void);
int monitor_ptp(void);
void *pmc_monitor(void *arg);
NotificationLevel tc_stat_notification_pull_fn(void *self, struct JsonValue **msg);


#endif // R2_MONITOR_H

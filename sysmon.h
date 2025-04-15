// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.

#ifndef R2_SYSMON_H
#define R2_SYSMON_H

//#include "log.h"
#include "notification.h"


bool init_monitor(void);
void finish_monitor(void);

bool register_tc_notification(bool add, char *target, unsigned period_ms);
bool register_modem_notification(bool add, char *target, unsigned period_ms);

#endif // R2_SYSMON_H

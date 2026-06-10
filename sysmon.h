// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.

#ifndef DNT_SYSMON_H
#define DNT_SYSMON_H

//#include "log.h"
#include "notification.h"


bool init_monitor(void);
void finish_monitor(void);

struct JsonValue *get_modem_state_json(const char *iface_name);
char *modem_sprintf_state_json(struct JsonValue *json, const char *record_sep, const char *line_sep);

bool register_tc_notification(bool add, char *target, unsigned period_ms);
bool register_modem_notification(bool add, char *target, unsigned period_ms);

#endif // DNT_SYSMON_H

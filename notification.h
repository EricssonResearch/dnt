// Copyright (c) 2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_NOTIFICATION_H
#define R2_NOTIFICATION_H

#include "json.h"

#include <stdbool.h>

// starts up the notification framework
// @conf_streams is the streams section read from the config (hash of ConfStream)
void init_notification(struct HashMap *conf_streams);

void finish_notification(void);

typedef enum {
    NOTIF_NONE,
    NOTIF_ERROR,
    NOTIF_WARNING,
    NOTIF_INFO,
    NOTIF_ALL
} NotificationLevel;

typedef NotificationLevel notification_pull_fn(struct JsonValue **);

// pull operation: all registered sources are periodically queried
// @name is the unique name of the source
// @callback will be called periodically to get a message
// @period_ms is the requested query period
// if @callback is NULL, this source is removed from the query list
// @returns false on failure
bool notification_register_source(const char *name, notification_pull_fn *callback, unsigned period_ms);

// push operation: anybody can submit a message anytime
// @source is the name of the source of the push
// @returns false if the notification system is not running
bool notification_push_event(const char *source, NotificationLevel level, struct JsonValue *message);

// set the minimum level that gets logged
// default is NOTIF_WARNING
// TODO separate for push and pull?
void notification_set_log_level(NotificationLevel level);

// set the minimum level that gets sent to the center
// default is NOTIF_ALL
// TODO separate for push and pull?
void notification_set_submit_level(NotificationLevel level);

#endif // R2_NOTIFICATION_H

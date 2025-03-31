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
    NOTIF_PULL,
    NOTIF_ALL
} NotificationLevel;

// it receives the @self that was registered, returns the message in @msg
typedef NotificationLevel notification_pull_fn(void *self, struct JsonValue **msg);

// pull operation: all registered sources are periodically queried
// @name is the unique name of the source
// @callback will be called periodically to get a message
// @self will be passed to the callback
// @period_ms is the requested query period
// if @callback is NULL, this source is removed from the query list
// @returns false on failure
bool notification_register_source(const char *name, notification_pull_fn *callback, void *self, unsigned period_ms);

// push operation: anybody can submit a message anytime
// @source is the name of the source of the push
// @returns false if the notification system is not running
bool notification_push_event(const char *source, NotificationLevel level, struct JsonValue *message);

// @returns true of the string is a valid log level
bool notification_level_valid(const char *level);

// unknown level is translated to NOTIF_NONE
NotificationLevel notification_level_from_string(const char *level);

// @returns a string representation of the given @level
const char *notification_string_from_level(NotificationLevel level);

// @returns the current minimum level that gets logged
NotificationLevel notification_log_level(void);

// @returns the current minimum level that gets sent to the center
NotificationLevel notification_submit_level(void);

// set the minimum level that gets logged
// default is NOTIF_WARNING
// TODO separate for push and pull?
void notification_set_log_level(NotificationLevel level);

// set the minimum level that gets sent to the center
// default is NOTIF_ALL
// TODO separate for push and pull?
void notification_set_submit_level(NotificationLevel level);

// 1 enable, 0 disable, -1 just return the current state
bool notification_enable_pull(int enable);

#endif // R2_NOTIFICATION_H

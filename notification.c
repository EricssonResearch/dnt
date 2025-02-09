// Copyright (c) 2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#include "notification.h"
#include "log.h"
#include "packet.h"
#include "pipeline.h"
#include "thread_utils.h"
#include "time_utils.h"
#include "utils.h"

#include "conf_actions.h"
#include "conf_streams.h"

#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <time.h>

DEFAULT_LOGGING_MODULE(NOTIFICATION, INFO);

#define MAX_NOTIFICATION_LEN 1000

#define DEFAULT_TIMEOUT 2*1000*1000

static struct Thread *notif_thread = NULL;
static struct MessageQueue *notif_q = NULL;

struct Pipeline *notification_pipe = NULL;

static struct HashMap *sources = NULL;
static pthread_mutex_t sources_lock = PTHREAD_MUTEX_INITIALIZER;

static NotificationLevel log_level = NOTIF_WARNING;
static NotificationLevel submit_level = NOTIF_ALL;

static unsigned notif_seq = 0;

struct NotificationSource {
    notification_pull_fn *pull;
    void *self;
    unsigned period_ms;
};

struct NotificationMessage {
    const char *source;
    NotificationLevel level;
    struct JsonValue *message;
};

static const char *notification_level_strings[] = {
    "NONE",
    "ERROR",
    "WARNING",
    "INFO",
    "ALL",
};

static void send_notification_packet(struct JsonValue *pkt)
{
    json_object_insert(pkt, "notif_seq", json_number(notif_seq++));

    unsigned pkt_len;
    char *pkt_str = json_serialize(pkt, &pkt_len);
    log_debug("sending %zu", strlen(pkt_str));

    struct Packet *packet = new_packet(NULL);
    packet_add_header(packet, 0, PROTO_ID_PAYLOAD, pkt_len);
    unsigned char *payload = packet->buf + packet->headers[0].start;
    memcpy(payload, pkt_str, pkt_len);
    free(pkt_str);
    struct PipelineIterator *pi = new_pipe_iterator(notification_pipe, packet);
    pipe_iterator_run(pi);
}

static void *notification_thread(void *arg)
{
    (void)arg;
    int period_us = 0;
    int timeout_us = -1;
    struct timespec next_wake;

    if (hashmap_count(sources) != 0) {
        //TODO find a period that fits all sources
        period_us = DEFAULT_TIMEOUT;
    }

    log_info("thread started, %s",
            notification_pipe ? "have notification session" : "no session");

    if (period_us > 0) {
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        next_wake = time_add_us(now, period_us);
        timeout_us = time_diff_us(next_wake, now);
    }
    log_debug("initial period %u first timeout %d", period_us, timeout_us);

    while (1) {
        struct NotificationMessage *msg = (struct NotificationMessage *)messagequeue_pop(notif_q, timeout_us);
        if (msg) {
            if (strcmp(msg->source, "notification_register_source") == 0) {
                free(msg);
                if (hashmap_count(sources) != 0) {
                    if (period_us == 0) {
                        // first source, start period now
                        clock_gettime(CLOCK_REALTIME, &next_wake);
                    }

                    //TODO find a period that fits all sources
                    period_us = DEFAULT_TIMEOUT;
                } else {
                    // no more sources, return to infinite timeout
                    period_us = 0;
                }
                log_debug("new period %u", period_us);
            } else {
                log_debug("push from '%s'", msg->source);
                unsigned js_len;
                char *js_str = json_serialize(msg->message, &js_len);
                if (msg->level <= log_level) {
                    if (msg->level == NOTIF_ERROR) {
                        log_error("push from '%s' '%s'", msg->source, js_str);
                    } else if (msg->level == NOTIF_WARNING) {
                        log_warning("push from '%s' '%s'", msg->source, js_str);
                    } else if (msg->level == NOTIF_INFO) {
                        log_info("push from '%s' '%s'", msg->source, js_str);
                    }
                }
                free(js_str);

                if ((msg->level <= submit_level) && notification_pipe) {
                    if (js_len + strlen(msg->source) < MAX_NOTIFICATION_LEN) {
                        struct JsonValue *pkt = json_object();
                        json_object_insert(pkt, msg->source, msg->message);
                        send_notification_packet(pkt);
                        json_delete(pkt);
                    } else {
                        log_error("can't send push from '%s' length %u", msg->source, js_len);
                    }
                } else {
                    json_delete(msg->message);
                }
                free(msg);
            }

            struct timespec now;
            clock_gettime(CLOCK_REALTIME, &now);
            if (period_us > 0) {
                timeout_us = time_diff_us(next_wake, now);
                if (timeout_us < 0) timeout_us = 0;
            } else {
                timeout_us = -1;
            }
            log_debug("after push period %u now %ld.%.9lu next_wake %ld.%.09lu timeout %d",
                    period_us, now.tv_sec, now.tv_nsec, next_wake.tv_sec, next_wake.tv_nsec, timeout_us);
        } else {
            // timeout, let's pull from everybody
            pthread_mutex_lock(&sources_lock);
            unsigned total_length = 0;
            struct JsonValue *pkt = json_object();
            HASHMAP_ITERATE(sources, s) { //TODO hashmap_foreach_sorted?
                struct NotificationSource *source = (struct NotificationSource *)hash_iterator_value(&s);
                const char *src = hash_iterator_key(&s);
                struct JsonValue *js;
                NotificationLevel level = source->pull(source->self, &js);
                if (js) {
                    log_debug("pull from '%s'", hash_iterator_key(&s));
                    unsigned js_len;
                    char *js_str = json_serialize(js, &js_len);

                    if (level <= log_level) {
                        if (level == NOTIF_ERROR) {
                            log_error("pull from '%s' '%s'", src, js_str);
                        } else if (level == NOTIF_WARNING) {
                            log_warning("pull from '%s' '%s'", src, js_str);
                        } else if (level == NOTIF_INFO) {
                            log_info("pull from '%s' '%s'", src, js_str);
                        }

                    }
                    free(js_str);

                    if ((level <= submit_level) && notification_pipe) {
                        if (total_length + js_len + strlen(src) < MAX_NOTIFICATION_LEN) {
                            json_object_insert(pkt, src, js);
                            total_length += js_len + strlen(src);
                        } else {
                            // send what we have collected, start new object
                            send_notification_packet(pkt);
                            json_delete(pkt);
                            pkt = json_object();
                        }
                    } else {
                        json_delete(js);
                    }
                }
            }
            pthread_mutex_unlock(&sources_lock);
            if (notification_pipe)
                send_notification_packet(pkt);
            json_delete(pkt);

            struct timespec now;
            clock_gettime(CLOCK_REALTIME, &now);
            next_wake = time_add_us(next_wake, period_us);
            timeout_us = time_diff_us(next_wake, now);
            if (timeout_us < 0) timeout_us = 0;

            log_debug("after timeout period %u now %ld.%.9lu next_wake %ld.%.09lu timeout %d",
                    period_us, now.tv_sec, now.tv_nsec, next_wake.tv_sec, next_wake.tv_nsec, timeout_us);
        }
    }

    return NULL;
}

void init_notification(struct HashMap *conf_streams)
{
    struct ConfStream *notif_sess = (struct ConfStream *)hashmap_find(conf_streams, "notification_session");
    if (notif_sess) {
        notification_pipe = assemble_actions("notification_session", notif_sess->actions);
        pipeline_ref_send_interfaces(notification_pipe);
    }
    if (sources == NULL)
        sources = new_hashmap(13, NULL, NULL);
    notif_q = new_messagequeue();
    notif_thread = thread_launch(notification_thread, NULL, "notification");
}

void finish_notification(void)
{
    notif_thread = thread_stop(notif_thread);
    notif_q = delete_messagequeue(notif_q);
    if (notification_pipe)
        pipeline_unref(notification_pipe);
    sources = delete_hashmap(sources);
}

bool notification_register_source(const char *name, notification_pull_fn *callback, void *self, unsigned period_ms)
{
    if (sources == NULL)
        sources = new_hashmap(13, NULL, NULL);

    struct NotificationSource *existing = (struct NotificationSource *)hashmap_find(sources, name);
    if (callback) {
        if (existing) {
            log_error("source '%s' already exists", name);
            return false;
        }

        struct NotificationSource *src = calloc_struct(NotificationSource);
        src->pull = callback;
        src->self = self;
        src->period_ms = period_ms;
        pthread_mutex_lock(&sources_lock);
        hashmap_insert(sources, strdup(name), src);
        pthread_mutex_unlock(&sources_lock);
        log_info("new source %s", name);
    } else {
        if (!existing) {
            log_error("can't remove unregistered source '%s'", name);
            return false;
        }

        pthread_mutex_lock(&sources_lock);
        hashmap_remove(sources, name);
        pthread_mutex_unlock(&sources_lock);
        log_info("removed source %s", name);
    }

    if (notif_q) {
        // notify the thread to recalculate its timeout
        struct NotificationMessage *msg = calloc_struct(NotificationMessage);
        msg->source = "notification_register_source";
        messagequeue_push(notif_q, msg);
    }
    return true;
}

bool notification_push_event(const char *source, NotificationLevel level, struct JsonValue *message)
{
    if (notif_q == NULL) {
        log_error("can't push without a queue");
        return false;
    }

    struct NotificationMessage *msg = calloc_struct(NotificationMessage);
    msg->source = source;
    msg->level = level;
    msg->message = message;
    messagequeue_push(notif_q, msg);
    return true;
}

bool notification_level_valid(const char *level)
{
    for (unsigned i=0; i<ARRAY_SIZE(notification_level_strings); i++) {
        if (strcmp(level, notification_level_strings[i]) == 0)
            return true;
    }
    return false;
}

NotificationLevel notification_level_from_string(const char *level)
{
    if (level == NULL) return NOTIF_NONE;
    for (unsigned i=0; i<ARRAY_SIZE(notification_level_strings); i++) {
        if (strcmp(level, notification_level_strings[i]) == 0)
            return (NotificationLevel)i;
    }
    return NOTIF_NONE;
}

const char *notification_string_from_level(NotificationLevel level)
{
    if (level >= 0 && level < ARRAY_SIZE(notification_level_strings))
        return notification_level_strings[level];
    return NULL;
}

NotificationLevel notification_log_level(void)
{
    return log_level;
}

NotificationLevel notification_submit_level(void)
{
    return submit_level;
}

void notification_set_log_level(NotificationLevel level)
{
    log_level = level;
}

void notification_set_submit_level(NotificationLevel level)
{
    submit_level = level;
}

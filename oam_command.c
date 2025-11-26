// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#define OAM_INTERNAL

#include "oam_command.h"
#include "oam_core.h"
#include "oam_maintenance.h"
#include "oam_request.h"
#include "oam_session.h"

#include "if_oam.h"
#include "if_oam_eth.h"
#include "log.h"
#include "notification.h"
#include "oam.h"
#include "replicate.h"
#include "seq_recov.h"
#include "state.h"
#include "thread_utils.h"
#include "utils.h"

#include "delay.h"
#include "sysmon.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>


DEFAULT_LOGGING_MODULE(OAM, INFO);

struct CommandConnection {
    char *name;
    char *remote_ip;
    unsigned short remote_port;
    int socket_fd; // RW
    FILE *cmd_w; // WRONLY
    int refcount;
    enum TerminalFormat mode;
    struct Thread *thread;
};


static struct HashMap *command_connections = NULL; // name -> struct command_connection
static pthread_mutex_t command_connections_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mask_lock = PTHREAD_MUTEX_INITIALIZER;

struct MaskObjectForeachArg {
    const char *stream;
    FILE *cmd_w;
    bool mask;
    unsigned found_count;
};


static const char *terminal_format_name(enum TerminalFormat f)
{
    switch (f) {
        case TF_DUMP:
            return "dump";
        case TF_JSON:
            return "json";
    }
    return NULL;
}

struct CommandConnection *find_command_connection(const char *name)
{
    if (name == NULL) return NULL;
    pthread_mutex_lock(&command_connections_lock);
    struct CommandConnection *ret = (struct CommandConnection *)hashmap_find(command_connections, name);
    if (ret)
        __atomic_fetch_add(&ret->refcount, 1, __ATOMIC_RELAXED);
    pthread_mutex_unlock(&command_connections_lock);
    return ret;
}

void release_command_connection(struct CommandConnection *conn)
{
    if (conn) {
        __atomic_fetch_sub(&conn->refcount, 1, __ATOMIC_RELAXED);
    }
}

bool command_connection_is_same(const struct CommandConnection *conn, const char *name)
{
    // no connection, no name -> same
    if (conn == NULL && name == NULL) return true;
    // have connection, have name -> compare
    if (conn && name) return strcmp(conn->name, name) == 0;
    // definitely different
    return false;
}

FILE *command_connection_get_w(struct CommandConnection *conn)
{
    if (conn == NULL) return NULL;
    return conn->cmd_w;
}

enum TerminalFormat command_connection_get_format(const struct CommandConnection *conn)
{
    return conn->mode;
}

static const char help_str[] =
    "Available commands:\n"
    "help, ? - get help\n"
    "exit, quit, CTRL+D - exit OAM\n"
    "log [module newlevel] - get current log levels or set it for the given module\n"
    "notify [{LOG|SUBMIT} newlevel] - get current notification levels or set them\n"
    "sysmon <command> <type> <target> [period_ms] - add/rem system monitoring. Type: delay, tc, modem. Target: specific\n"
    "notif_pull [enable|disable] - enable or disable the pull notifications\n"
    "mode [mode] - set ping reply printing mode, can be 'dump' or 'json'\n"
    "ue <tty> - show modem statistics\n"
    "list - list monitoring start points\n"
    "iface [ifname] - print information about interfaces\n"
    "mp [mpname] - print information about maintenance points\n"
    "object [objname] - print information about pipeline objects\n"
    "returns - list return interfaces\n"
    "sessions [stream] - list active sessions for stream, lists all sessions if no 'stream' is specified\n"
    "[un]mask <replication pipeline> - mask/unmask a replication pipeline\n"
    "rlist[@if] <mep-start/mip> <mep-stop/mip/any> <level> - list monitoring start points of the remote node\n"
    "ping[@if] <mep-start/mip> <mep-stop/mip/any> <level> [-r] [-o] [-i <interval>] [-n <count>] [-t <ttl>]\n"
    "rping[@if] <mep-start/mip> <mep-stop/mip> <level> <remote mep-start/mip> <remote mep-stop/mip/any> <remote level> [-r] [-o] [-i <interval>] [-n <count>] [-t <ttl>]\n"
    "notif_trigger <mep-start> <mep-stop/mip/any> <level> [-i <interval>] [-n <count>] [-t <ttl>]\n"
    "stop [stream session_id] - stop a running OAM session identified by 'stream:session_id', without parameter it stops the last session\n"
    ;

static int list_startpoints_cb(const char *key, void *value, void *userdata)
{
    (void)key;
    struct OAM_MaintenancePoint *mp = (struct OAM_MaintenancePoint*)value;
    FILE *cmd_w = (FILE *)userdata;
    if (mp_get_type(mp) != OAM_Stop) {
        mp_print_info(mp, cmd_w, false);
        fprintf(cmd_w, "\n");
    }
    return 1;
}

static int list_mp_cb(const char *key, void *value, void *userdata)
{
    (void)key;
    struct OAM_MaintenancePoint *mp = (struct OAM_MaintenancePoint*)value;
    FILE *cmd_w = (FILE *)userdata;
    mp_print_info(mp, cmd_w, false);
    fprintf(cmd_w, "\n");
    return 1;
}

static int list_oam_ifaces_cb(const char *ifname, void *value, void *userdata)
{
    struct Interface *iface = (struct Interface *)value;
    FILE *cmd_w = (FILE *)userdata;

    if (iface->type == IF_OAM) {
        const char *return_ip = oamif_get_ip(iface);
        unsigned return_port = oamif_get_port(iface);
        fprintf(cmd_w, "  %s ip %s port %u",
                ifname, return_ip, return_port);
        if (iface == get_default_oam_ip_interface())
            fprintf(cmd_w, " (default for UDP, node id %u)", oamif_get_uid(iface));
    } else if (iface->type == IF_OAM_ETH) {
        const char *return_mac = oam_eth_if_get_mac(iface);
        fprintf(cmd_w, "  %s mac %s",
                ifname, return_mac);
        if (iface == get_default_oam_eth_interface())
            fprintf(cmd_w, " (default for ETH, node id %u)", oam_eth_if_get_uid(iface));
    }
    fprintf(cmd_w, "\n");

    return 1;
}

static int iface_info_cb(struct Interface *iface, void *userdata)
{
    FILE *cmd_w = (FILE *)userdata;
    iface_print_info(iface, cmd_w, false);
    return 1;
}

static int list_objects_cb(struct PipelineObject *obj, void *userdata)
{
    FILE *cmd_w = (FILE *)userdata;
    pipelineobject_print_info(obj, cmd_w);
    return 1;
}

static int list_log_modules_cb(const char *mod_name, LOGGING_LEVELS current_level, void *userdata)
{
    FILE *cmd_w = (FILE *)userdata;
    fprintf(cmd_w, "  %s level %s\n", mod_name, log_string_from_level(current_level));
    return 1;
}

static int query_mask_cb(struct PipelineObject *obj, void *userdata)
{
    FILE *cmd_w = (FILE*)userdata;

    if (obj->type == PO_REPL) {
        //TODO replicate_report_mask_state(obj, cmd_w);
        //      problem: we use too many oam-internal things in this loop
        fprintf(cmd_w, "mask state for Replicate '%s'\n", obj->name);
        struct PipelineList *rlist = replicate_get_pipes(obj);
        while (rlist) {
            if (rlist->pipe->mask && obj->auto_mip_level >= 0) {
                char *mipname = oam_automip_name(rlist->pipe->name, obj->auto_mip_level, obj->name, true);
                struct OAM_MaintenancePoint *mip = find_maintenance_point(mipname);
                free(mipname);
                if (mip) {
                    fprintf(cmd_w, "  pipeline '%s' is %s, ", rlist->pipe->name,
                            rlist->pipe->mask ? "masked" : "not masked");
                    mp_print_mask_signalling_state(mip, cmd_w);
                    oam_unref_maintenance_point(mip);
                }
            } else {
                fprintf(cmd_w, "  pipeline '%s' is %s\n", rlist->pipe->name,
                        rlist->pipe->mask ? "masked" : "not masked");
            }

            rlist = rlist->next;
        }
    } else if (obj->type == PO_SEQREC) {
        seq_rec_report_mask_state(obj, cmd_w);
    }

    return 1;
}

static int set_mask_cb(struct PipelineObject *obj, void *userdata)
{
    struct MaskObjectForeachArg *arg = (struct MaskObjectForeachArg *)userdata;

    if (obj->type == PO_REPL) {
        //TODO move this loop to the replicate object somehow....
        struct PipelineList *rlist = replicate_get_pipes(obj);
        while (rlist) {
            if (strcmp(rlist->pipe->name, arg->stream) == 0) {
                arg->found_count++;
                if (rlist->pipe->mask == arg->mask) {
                    fprintf(arg->cmd_w, "Pipeline '%s' in Replicate %s already %smasked\n",
                            arg->stream, obj->name, arg->mask ? "" : "un");
                } else {
                    rlist->pipe->mask = arg->mask;

                    struct JsonValue *noti = json_object();
                    json_object_insert(noti, "replication_pipeline", json_string(rlist->pipe->name));
                    json_object_insert(noti, "status", json_string("masked"));
                    notification_push_event("mask", NOTIF_INFO, noti);

                    fprintf(arg->cmd_w, "Pipeline '%s' in Replicate %s now %smasked\n",
                            arg->stream, obj->name, arg->mask ? "" : "un");

                    if (obj->auto_mip_level >= 0) {
                        char *mipname = oam_automip_name(arg->stream, obj->auto_mip_level, obj->name, true);
                        struct OAM_MaintenancePoint *mip = find_maintenance_point(mipname);

                        if (mip) {
                            // we mustdn't keep holding this reference
                            oam_unref_maintenance_point(mip);
                            if (arg->mask) {
                                mp_initiate_mask_signalling(mip, arg->cmd_w);
                            } else {
                                mp_stop_mask_signalling(mip, arg->cmd_w);
                            }
                        } else {
                            fprintf(arg->cmd_w, "ERROR can't find automip '%s' for object '%s'\n",
                                    mipname, obj->name);
                        }

                        free(mipname);
                    }
                }
            }
            rlist = rlist->next;
        }
    }

    return 1;
}

static void mask_query(FILE *cmd_w)
{
    state_foreach_objects(query_mask_cb, cmd_w);
}

static void mask_set(const char *stream, FILE *cmd_w)
{
    struct MaskObjectForeachArg arg = { .stream = stream, .cmd_w = cmd_w, .mask = true, .found_count = 0 };
    pthread_mutex_lock(&mask_lock);
    state_foreach_objects(set_mask_cb, &arg);
    pthread_mutex_unlock(&mask_lock);
    if (arg.found_count == 0) fprintf(cmd_w, "No pipelines are named '%s'\n", stream);
}

static void mask_unset(const char *stream, FILE *cmd_w)
{
    struct MaskObjectForeachArg arg = { .stream = stream, .cmd_w = cmd_w, .mask = false, .found_count = 0 };
    pthread_mutex_lock(&mask_lock);
    state_foreach_objects(set_mask_cb, &arg);
    pthread_mutex_unlock(&mask_lock);
    if (arg.found_count == 0) fprintf(cmd_w, "No pipelines are named '%s'\n", stream);
}

#define TELNET_IAC         0xffu /* Interpret As Command */
#define TELNET_INTERRUPT   0xf4u /* interrupt process */
#define TELNET_WILL        0xfbu
#define TELNET_WONT        0xfcu
#define TELNET_DO          0xfdu
#define TELNET_DONT        0xfeu
#define TELNET_TIMING_MARK    6 /* see RFC 860 */
static void handle_telnet_command(unsigned char *oam_command, int *n, FILE *cmd_w)
{
    int k = 0;
    bool stop = false;

    while (oam_command[k] == TELNET_IAC) {
        if (*n > k+1) {
            if (oam_command[k+1] == TELNET_INTERRUPT) {
                stop = true;
                k += 2;
            } else if (oam_command[k+1] == TELNET_DO) {
                if (*n > k+2) {
                    if (oam_command[k+2] == TELNET_TIMING_MARK) {
                        // we must reply with this or the telnet client gets stuck (see RFC 860)
                        unsigned char reply[] = {TELNET_IAC, TELNET_WILL, TELNET_TIMING_MARK, 0};
                        fprintf(cmd_w, "%s", reply);
                        k += 3;
                    } else {
                        log_error("unhandled telnet DO command %d", oam_command[k+2]);
                        k += 3;
                    }
                } else {
                    log_error("incomplete 2 byte telnet DO command %d received", oam_command[k+2]);
                    k += 2;
                }
            } else {
                log_error("unhandled telnet command %d", oam_command[k+1]);
                k += 2; //TODO we don't know how long this command is
            }
        } else {
            log_error("incomplete 1 byte telnet command received");
            k += 1;
        }
        if (k >= *n) break;
    }

    // remove the processed commands by moving everything forward
    if (k > 0) {
        memmove(oam_command, oam_command+k, *n-k+1); // include the closing 0
        *n -= k;
    }

    // interpret the interrupt command (ctrl+C) as "stop"
    if (stop) {
        memmove(oam_command+4, oam_command, *n+1); // include the closing 0
        const char *stop_s = "stop";
        memcpy(oam_command, stop_s, 4);
        *n += 4;
    }
}

static void command_loop(struct CommandConnection *conn)
{
#define ERROR(msg, ...)                             \
    fprintf(cmd_w, "Error: " msg "\n",              \
            ##__VA_ARGS__);                         \
    continue

#define CHECK_REQUEST(req)                                      \
    if (request_get_error(req)) {                               \
        fprintf(cmd_w, "Error: %s command is invalid: %s\n",    \
            request_get_type(req), request_get_error(req));     \
        delete_oam_request(req);                                \
        continue;                                               \
    }


    int cmd_fd = conn->socket_fd;
    FILE *cmd_w = conn->cmd_w;

    char oam_command[255], last_command[255];
    char streamname[32];

    // stream name and session id of the last issued command
    char *last_stream = NULL;
    unsigned last_session = 0;

    while (conn->name == NULL) usleep(1000); // this is generated after the thread has been launched

    fprintf(cmd_w, "\033[32mOAM '%s' ready\033[0m%s\n", conn->name,
            (have_default_ip_iface() || have_default_eth_iface())?"":", but has no configured return interface");

    log_info("Telnet connection '%s' from %s %u", conn->name, conn->remote_ip, conn->remote_port);
    struct JsonValue *msg = json_object();
    json_object_insert(msg, "login", json_string(conn->name));
    json_object_insert(msg, "ip", json_string(conn->remote_ip));
    json_object_insert(msg, "port", json_number(conn->remote_port));
    notification_push_event("telnet", NOTIF_INFO, msg);

    while (true) {
        int n = read(cmd_fd, oam_command, sizeof(oam_command)-1);
        if (n > 0) {
            oam_command[n] = 0;

            if ((unsigned char)(oam_command[0]) == TELNET_IAC) {
                handle_telnet_command((unsigned char*)oam_command, &n, cmd_w);
                if (n == 0) continue;
            }

            // cut off traling whitespace and newline
            while (n > 0 && (isspace(oam_command[n-1]) || oam_command[n-1] == '\n' || oam_command[n-1] == '\r'))
                oam_command[--n] = 0;
            //printf("oam command '%s' length %d\n", oam_command, n);

            if (n == 0) continue;

            // "up key" means "get the last command from memory"
            if (strcmp(oam_command, "\x1b[A") == 0) {
                strcpy(oam_command, last_command);
                fprintf(cmd_w, "%s\n", oam_command);
            } else {
                char *p = oam_command;
                while (isspace(*p)) p++;
                if (*p != 0)
                    strcpy(last_command, oam_command);
                else
                    continue;
            }

            // ASCII 4 means End of Transmission (CTRL+D)
            if( (strcmp(oam_command, "exit") == 0) || (strcmp(oam_command, "quit") == 0 || oam_command[0] == 4) ){
                log_debug("telnet exit command");
                fprintf(cmd_w, "Exiting.\n");
                break;
            }
            else if(strncmp(oam_command, "mode", 4) == 0){
                char *mode_str = oam_command + 4;
                while (isspace(*mode_str)) mode_str++;
                if (*mode_str) {
                    if (strcmp(mode_str, "dump") == 0) {
                        conn->mode = TF_DUMP;
                    } else if (strcmp(mode_str, "json") == 0) {
                        conn->mode = TF_JSON;
                    }else{
                        ERROR("mode argument is invalid");
                    }
                }
                fprintf(cmd_w, "Display mode is %s\n", terminal_format_name(conn->mode));
            }
            else if (strcmp(oam_command, "help") == 0 || strcmp(oam_command, "?") == 0) {
                fprintf(cmd_w, help_str);
            }
            else if (strcmp(oam_command, "list") == 0) {
                fprintf(cmd_w, "Available MEP Start points:\n");
                foreach_mp(true, list_startpoints_cb, cmd_w);
            }
            else if (strncmp(oam_command, "iface", 5) == 0) {
                char ifname[64];
                int k = sscanf(oam_command, "iface %s", ifname);
                if (k == 0 || k == EOF) {
                    state_foreach_interfaces(iface_info_cb, cmd_w);
                } else {
                    struct Interface *iface = state_get_interface(ifname);
                    if (iface) {
                        iface_print_info(iface, cmd_w, true);
                    } else {
                        fprintf(cmd_w, "No interface named '%s'\n", ifname);
                    }
                }
            }
            else if (strncmp(oam_command, "mp", 2) == 0) {
                char mpname[64];
                int k = sscanf(oam_command, "mp %s", mpname);
                if (k == 0 || k == EOF) {
                    foreach_mp(true, list_mp_cb, cmd_w);
                } else {
                    struct OAM_MaintenancePoint *mp = find_maintenance_point(mpname);
                    if (mp) {
                        mp_print_info(mp, cmd_w, true);
                        fprintf(cmd_w, "\n");
                    } else {
                        fprintf(cmd_w, "No maintenance point named '%s'\n", mpname);
                    }
                }
            }
            else if (strncmp(oam_command, "object", 6) == 0) {
                char objname[64];
                int k = sscanf(oam_command, "object %s", objname);
                if (k == 0 || k == EOF) {
                    state_foreach_objects(list_objects_cb, cmd_w);
                } else {
                    struct PipelineObject *obj = state_get_object(objname);
                    if (obj) {
                        pipelineobject_print_info(obj, cmd_w);
                    } else {
                        fprintf(cmd_w, "No pipeline object named '%s'\n", objname);
                    }
                }
            }
            else if (strncmp(oam_command, "log", 3) == 0) {
                char modulename[64];
                char newlevel[16];
                int k = sscanf(oam_command, "log %s %s", modulename, newlevel);
                if (k == 0 || k == EOF) {
                    fprintf(cmd_w, "Logging modules:\n");
                    log_get_levels(list_log_modules_cb, cmd_w);
                } else if (k == 2) {
                    if (!log_level_valid(newlevel)) {
                        fprintf(cmd_w, "Invalid log level '%s'\n", newlevel);
                    } else {
                        LOGGING_LEVELS nlvl = log_level_from_string(newlevel);
                        if (!log_set_level(modulename, nlvl)) {
                            fprintf(cmd_w, "Module '%s' does not exist.\n", modulename);
                        } else {
                            fprintf(cmd_w, "Module '%s' new level %s.\n", modulename, log_string_from_level(nlvl));
                        }
                    }
                } else {
                    fprintf(cmd_w, "Invalid parameters for 'log' command.\n");
                }
            }
            else if (strncmp(oam_command, "notify", 6) == 0) {
                char target[32];
                char newlevel[16];
                int k = sscanf(oam_command, "notify %s %s", target, newlevel);
                if (k == 0 || k == EOF) {
                    fprintf(cmd_w, "Notification level limits:\n  LOG    %s\n  SUBMIT %s\n",
                            notification_string_from_level(notification_log_level()),
                            notification_string_from_level(notification_submit_level()));
                } else if (k == 2) {
                    if (notification_level_valid(newlevel)) {
                        NotificationLevel nlvl = notification_level_from_string(newlevel);
                        if (strcmp(target, "LOG") == 0) {
                            notification_set_log_level(nlvl);
                        } else if (strcmp(target, "SUBMIT") == 0) {
                            notification_set_submit_level(nlvl);
                        } else {
                            fprintf(cmd_w, "Invalid notification target '%s'.\n", target);
                        }
                    } else {
                        fprintf(cmd_w, "Invalid notification level '%s'.\n", newlevel);
                    }
                } else {
                    fprintf(cmd_w, "Invalid parameters for 'notify' command.\n");
                }
            }
            else if (strncmp(oam_command, "sysmon", 6) == 0) {
                char target[32];
                char type[16];
                char cmd[16];
                unsigned period_ms = 2000;
                bool ret = false, add = false;
                int k = sscanf(oam_command, "sysmon %s %s %s %u", cmd, type, target, &period_ms);
                if (k <= 2 || k == EOF) {
                    fprintf(cmd_w, "sysmon <cmd> <type> <target> [period]\n");
                } else if (k <= 4) {
                    if ((strcmp(cmd, "add") == 0) || (strcmp(cmd, "rem") == 0)) {   // rem instead of del to avoid mixing up with delay
                        if (strcmp(cmd, "add") == 0)
                            add = true;
                        if (strcmp(type, "tc") == 0) {
                            ret = register_tc_notification(add, target, period_ms);
                        } else if (strcmp(type, "delay") == 0) {
                            ret =register_delay_notification(add, target, period_ms);
                        } else if (strcmp(type, "modem") == 0) {
                            ret = register_modem_notification(add, target, period_ms);
                        } else {
                            fprintf(cmd_w, "Invalid monitor type '%s'.\n", type);
                        }
                        if(ret)
                            fprintf(cmd_w, "Success\n");
                        else
                            fprintf(cmd_w, "Error sysmon %s %s %s.\n", cmd, type, target);
                    } else {
                        fprintf(cmd_w, "Invalid command '%s'. Command should be 'add' or 'rem'\n", cmd);
                    }
                } else {
                   fprintf(cmd_w, "Invalid parameters for 'sysmon' command.\n");
               }
            }
            else if (strncmp(oam_command, "ue", 2) == 0) {
                char ttyname[64];
                int k = sscanf(oam_command, "ue %s", ttyname);
                if (k == 0 || k == EOF) {
                    fprintf(cmd_w, "ue <ttyUSBn>\n");
                } else {
                    struct JsonValue *js = get_modem_state_json(ttyname);
                    if(js != NULL) {
                        if(conn->mode == TF_JSON){
                            unsigned len;
                            char *jstr = json_serialize(js, &len);
                            fprintf(cmd_w, "%s\n", jstr);
                            free(jstr);
                        }else
                            fprintf(cmd_w, "%s", modem_sprintf_state_json(js, ", ", "\n"));
                        json_delete(js);
                    }
                    else
                        fprintf(cmd_w, "Error, wrong tty?\n");
                }
            }
            else if (strncmp(oam_command, "sessions", 8) == 0) {
                int k=sscanf(oam_command, "sessions %s", streamname);
                if (k==0 || k==EOF) {
                    list_sessions_of_all_streams(cmd_w);
                }
                else if (k==1) {
                    struct StreamSessions *stream = get_stream_sessions(streamname);
                    if (stream == NULL) {
                        fprintf(cmd_w, "Invalid stream name '%s'.\n", streamname);
                    } else {
                        if (list_sessions_of_stream(stream, streamname, conn->cmd_w) == 0)
                            fprintf(cmd_w, "Stream %s has no sessions\n", streamname);
                    }
                }
                else {
                    fprintf(cmd_w, "Invalid parameters for 'sessions' command.\n");
                }
            }
            else if (strncmp(oam_command, "stop", 4) == 0) {
                unsigned session;
                int k=sscanf(oam_command, "stop %s %u", streamname, &session);
                if (k==0 || k==EOF) {
                    if (last_stream == NULL) {
                        fprintf(cmd_w, "No previous command to stop.\n");
                    } else {
                        int res = stop_session(last_stream, last_session);
                        fprintf(cmd_w, "Stopping stream:session %s:%d - %s\n",
                                last_stream, -1,
                                res > 0 ? "stopped" : "not running");
                        free(last_stream);
                        last_stream = 0;
                    }
                } else if (k==2) {
                    int res = stop_session(streamname, session);
                    fprintf(cmd_w, "Stopping stream:session %s:%d - %s\n",
                            streamname, session,
                            res > 0 ? "stopped" : "not running");
                    if (strcmp(streamname, last_stream) == 0 && session == last_session) {
                        free(last_stream);
                        last_stream = 0;
                    }
                } else {
                    fprintf(cmd_w, "invalid parameters for stop.\n");
                }
            }
            else if (strcmp(oam_command, "returns") == 0) {
                fprintf(cmd_w, "Available OAM return interfaces:\n");
                foreach_oam_ifaces(list_oam_ifaces_cb, conn->cmd_w);
            }
            else if (strncmp(oam_command, "ping", 4) == 0) {
                struct OamRequest *ping_req = parse_ping_command(oam_command+4, true, true);
                CHECK_REQUEST(ping_req);
                if (!initiate_request(ping_req, conn->name)) {
                    ERROR("sending ping command failed: %s", request_get_error(ping_req));
                    delete_oam_request(ping_req);
                }
                if (last_stream)
                    free(last_stream);
                last_stream = strdup(request_get_stream_name(ping_req));
                last_session = request_get_session_id(ping_req);
            }
            else if (strncmp(oam_command, "rping", 5) == 0) {
                struct OamRequest *rping_req = parse_rping_command(oam_command+5);
                CHECK_REQUEST(rping_req);
                if (!initiate_request(rping_req, conn->name)) {
                    ERROR("sending rping command failed: %s", request_get_error(rping_req));
                    delete_oam_request(rping_req);
                }
            }
            else if (strncmp(oam_command, "notif_trigger", 13) == 0) {
                struct OamRequest *trig_req = parse_trigger_command(oam_command+13, true);
                CHECK_REQUEST(trig_req);
                if (!initiate_request(trig_req, conn->name)) {
                    ERROR("sending notif_trigger command failed: %s", request_get_error(trig_req));
                    delete_oam_request(trig_req);
                }
            }
            else if (strncmp(oam_command, "notif_pull", 10) == 0) {
                if (strlen(oam_command) == 10) {
                    // no params, just query the state
                    fprintf(cmd_w, "Notification pull is %s\n", notification_enable_pull(-1) ? "enabled" : "disabled");
                } else {
                    // parse the param
                    if (strcmp(oam_command+10, " enable") == 0) {
                        notification_enable_pull(1);
                        fprintf(cmd_w, "Notification pull is now enabled\n");
                    } else if (strcmp(oam_command+10, " disable") == 0) {
                        notification_enable_pull(0);
                        fprintf(cmd_w, "Notification pull is now disabled\n");
                    } else {
                        fprintf(cmd_w, "Notification pull setting '%s' is invalid\n", oam_command+10);
                    }
                }
            }
            else if (strncmp(oam_command, "rlist", 5) == 0) {
                struct OamRequest *rlist_req = parse_rlist_command(oam_command+5);
                CHECK_REQUEST(rlist_req);
                if (!initiate_request(rlist_req, conn->name)) {
                    ERROR("sending rlist command failed: %s", request_get_error(rlist_req));
                    delete_oam_request(rlist_req);
                }
            }
            else if (!strncmp(oam_command, "mask", 4)) {
                int k=sscanf(oam_command, "mask %s", streamname);
                if (k==0 || k==EOF) {
                    mask_query(cmd_w);
                }
                else if (k==1) {
                    mask_set(streamname, cmd_w);
                }
                else {
                    fprintf(cmd_w, "Invalid parameters for 'mask' command.\n");
                }
            }
            else if (!strncmp(oam_command, "unmask", 6)) {
                int k=sscanf(oam_command, "unmask %s", streamname);
                if (k==0 || k==EOF) {
                    mask_query(cmd_w);
                }
                else if (k==1) {
                    mask_unset(streamname, cmd_w);
                }
                else {
                    fprintf(cmd_w, "Invalid parameters for 'unmask' command.\n");
                }
            }
            else {
                ERROR("unknown command '%s'", oam_command);
            }
        }
        else {
            if (n < 0) {
                log_perror("oam commandline read");
            }
            log_debug("remote closed the telnet socket without 'quit'");
            break;
        }
    }
    log_info("Telnet closed");
    free(last_stream);

#undef ERROR
#undef CHECK_REQUEST
}

static void *command_thread(void *arg)
{
    struct CommandConnection *conn = (struct CommandConnection *)arg;
    command_loop(conn);
    struct Thread *thread = conn->thread;
    pthread_mutex_lock(&command_connections_lock);
    // the hash delete callback will call thread_stop, but it does nothing to its own thread
    hashmap_remove(command_connections, conn->name);
    pthread_mutex_unlock(&command_connections_lock);
    thread_exit(thread);
    return NULL;
}

void oam_start_command_connection(int fd, const char *remote_ip, unsigned short remote_port)
{
    struct CommandConnection *conn = calloc_struct(CommandConnection);
    conn->socket_fd = fd;
    // note: inverse operation is fd=fileno(file)
    conn->cmd_w = fdopen(fd, "w");
    //TODO if we want to fread() we need to duplicate the handle
    //int cmd_fd_dup = dup(cmd_fd);
    //FILE *cmd_r = fdopen(cmd_fd_dup, "r");

    setvbuf(conn->cmd_w, NULL, _IOLBF, 0);
    conn->remote_ip = strdup(remote_ip);
    conn->remote_port = remote_port;

    conn->thread = thread_launch(command_thread, conn, "command");
    if (conn->thread == NULL) {
        log_perror("could not create new oam thread");
        fclose(conn->cmd_w); // we only need to close the FILE*
        free(conn->remote_ip);
        free(conn);
        return;
    }
    conn->name = strdup_printf("conn %u", thread_getid(conn->thread));
    pthread_mutex_lock(&command_connections_lock);
    hashmap_insert(command_connections, conn->name, conn);
    pthread_mutex_unlock(&command_connections_lock);
}

void oam_cli_alert(const char *fmt, ...)
{
    char msg[200];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    pthread_mutex_lock(&command_connections_lock);
    HASHMAP_ITERATE(command_connections, it) {
        struct CommandConnection *conn = (struct CommandConnection *)hash_iterator_value(&it);
        fprintf(conn->cmd_w, "\n\33[1;33m%s\033[0m\n", msg);
    }
    pthread_mutex_unlock(&command_connections_lock);
}

static int command_connection_delete_cb(const char *key, void *value, void *userdata)
{
    (void)key; // same as conn->name
    (void)userdata;
    struct CommandConnection *conn = (struct CommandConnection *)value;
    while (conn->refcount > 0) {
        usleep(1000);
    }
    if (conn->cmd_w) fclose(conn->cmd_w); // we only need to close the FILE*
    conn->cmd_w = NULL;
    stop_all_sessions_of_connection(conn);
    thread_stop(conn->thread); //TODO this leaks last_stream
    free(conn->name);
    free(conn->remote_ip);
    free(conn);
    return 1;
}

void init_cmd_module(void)
{
    command_connections = new_hashmap(7, command_connection_delete_cb, NULL);
}

void finish_cmd_module(void)
{
    pthread_mutex_lock(&command_connections_lock);
    delete_hashmap(command_connections);
    pthread_mutex_unlock(&command_connections_lock);
}

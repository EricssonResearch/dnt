// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#define OAM_INTERNAL

#include "oam.h"
#include "oam_command.h"
#include "oam_core.h"
#include "oam_message.h"

#include "if_oam.h"
#include "log.h"
#include "thread_utils.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#include <unistd.h>
#include <arpa/inet.h>


DEFAULT_LOGGING_MODULE(OAM, INFO);

static struct HashMap *command_connections = NULL; // name -> struct command_connection

const char *terminal_format_name(enum TerminalFormat f)
{
    switch (f) {
        case TF_DUMP:
            return "dump";
        case TF_JSON:
            return "json";
    }
    return NULL;
}

struct command_connection *find_command_connection(const char *name)
{
    if (name == NULL) return NULL;
    return hashmap_find(command_connections, name);
}

bool command_connection_is_same(const struct command_connection *conn, const char *name)
{
    // no connection, no name -> same
    if (conn == NULL && name == NULL) return true;
    // have connection, have name -> compare
    if (conn && name) return strcmp(conn->name, name) == 0;
    // definitely different
    return false;
}

FILE *command_connection_get_w(struct command_connection *conn)
{
    if (conn == NULL) return NULL;
    __atomic_fetch_add(&conn->w_users, 1, __ATOMIC_RELAXED);
    return conn->cmd_w;
}

void command_connection_release_w(struct command_connection *conn)
{
    if (conn == NULL) return;
    __atomic_fetch_sub(&conn->w_users, 1, __ATOMIC_RELAXED);
}


// parses @str, accepts 'ipv4' 'ipv4:port', 'ipv6', '[ipv6]', '[ipv6]:port'
// TODO accept domain names?
// allocates a new string for @ip
// @returns true on success, otherwise doesn't touch the output parameter
// TODO move this to common utils
static bool parse_ip_port(const char *str, char **ip, unsigned *port)
{
    if (str[0] == '[') {
            int n=1;
            while (str[n]) {
                if (str[n] == ']') break;
                n++;
            }
            if (str[n] == ']') {
                char *ip6_s = strndup(str+1, n-1);
                struct in6_addr ip6;
                if (inet_pton(AF_INET6, ip6_s, &ip6) != 1) {
                    free(ip6_s);
                    return false;
                }
                n++;
                if (str[n]) {
                    unsigned p;
                    char err;
                    if (sscanf(str+n, ":%u%c", &p, &err) != 1) {
                        free(ip6_s);
                        return false;
                    }
                    *port = p;
                }
                *ip = ip6_s;
                return true;
            } else {
                // missing ']'
                return false;
            }
    } else {
        struct in6_addr ip6;
        if (inet_pton(AF_INET6, str, &ip6) == 1) {
            // IPv6 without port
            *ip = strdup(str);
            return true;
        } else {
            char *colon = strchr(str, ':');
            if (colon) {
                char *colon2 = strchr(colon+1, ':');
                if (colon2) {
                    return false;
                }
                char *ip_s = strndup(str, colon-str);
                struct in_addr ip4;
                //TODO check with getaddrinfo to accept domain name?
                if (inet_pton(AF_INET, ip_s, &ip4) != 1) {
                    free(ip_s);
                    return false;
                }
                unsigned p;
                char err;
                if (sscanf(colon, ":%u%c", &p, &err) != 1) {
                    free(ip_s);
                    return false;
                }
                *ip = ip_s;
                *port = p;
                return true;
            } else {
                // no port
                char *ip_s = strdup(str);
                struct in_addr ip4;
                //TODO check with getaddrinfo to accept domain name?
                if (inet_pton(AF_INET, ip_s, &ip4) != 1) {
                    free(ip_s);
                    return false;
                }
                *ip = ip_s;
                return true;
            }
        }
        return false;
    }
}

static bool parse_ping_returnif(struct oam_request *ping_req, const char *ifname)
{
    struct Interface *iface = get_oam_interface(ifname);
    if (iface == NULL) {
        ping_req->return_port = OAM_PORT;
        if (parse_ip_port(ifname, &ping_req->return_ip, &ping_req->return_port)) {
            ping_req->node_id = get_node_id();
            log_debug("return ip '%s' port %u\n", ping_req->return_ip, ping_req->return_port);
            return true;
        }
        if (have_default_iface()) {
            ping_req->error = strdup_printf("invalid return interface name: %s", ifname);
        } else {
            ping_req->error = strdup("need a return interface or a remote IP to send requests");
        }
        return false;
    }
    ping_req->node_id = oamif_get_uid(iface);
    ping_req->return_ip = strdup(oamif_get_ip(iface));
    ping_req->return_port = oamif_get_port(iface);
    return true;
}

static bool parse_ping_options(struct oam_request *ping_req, const char *options_str, bool allow_num)
{
    const char *po = options_str;
    bool opt_err = false;
    int k, l;
    int val;
    float fval;
    char c;

    while ((k=sscanf(po, " -%c%n", &c, &l)) == 1) {
        if (!isspace(*po)) {
            ping_req->error = strdup("Error: ping options must be separated by space");
            opt_err = true;
            break;
        }
        po += l;
        if (c=='r') {
            ping_req->record_route = true;
        } else if (c=='o') {
            ping_req->object_state = true;
        } else if (c=='d') {
            ping_req->delay = true;
        } else if (c=='i') {
            k = sscanf(po, " %f%n", &fval, &l);
            if (k == 1) {
                po += l;
                if (fval < 0.002) fval = 0.002; // 2msec is the minimum
                ping_req->interval_ms = fval * 1000;
            } else {
                ping_req->error = strdup("ping interval is invalid");
                opt_err = true;
                break;
            }
        } else if (c=='n') {
            if(!allow_num){
                ping_req->error = strdup("ping count is not allwed in config");
                opt_err = true;
                break;
            }
            k = sscanf(po, " %d%n", &val, &l);
            if (k == 1) {
                po += l;
                ping_req->count = val;
            } else {
                ping_req->error = strdup("ping count is invalid\n");
                opt_err = true;
                break;
            }
        } else if (c=='t') {
            k = sscanf(po, " %d%n", &val, &l);
            if (k == 1) {
                po += l;
                ping_req->ttl = val;
            } else {
                ping_req->error = strdup("ping ttl is invalid");
                opt_err = true;
                break;
            }
        } else {
            ping_req->error = strdup_printf("ping option '%c' is invalid", c);
            opt_err = true;
            break;
        }
    }
    if (opt_err) return false;
    while (isspace(*po)) po++;
    if (*po) {
        ping_req->error = strdup_printf("ping options '%s' is invalid", po);
        return false;
    }
    return true;
}

// always returns a request, sets ret->error to an error message
struct oam_request *parse_ping_command(const char *oam_command, bool allow_returniface, bool allow_num,
        struct command_connection *conn)
{
    int l;
    char start_name[32];
    char iface_name[64];

    struct oam_request *ping_req = new_oam_request("ping", conn);

    if (oam_command[0]=='@') {
        if (!allow_returniface) {
            ping_req->error = strdup("ping return interface is not allowed");
            return ping_req;
        }
        int k = sscanf(oam_command, "@%s %s %s %d%n",
                       iface_name, start_name, ping_req->mep_stop, &ping_req->level, &l);
        if (k < 4) {
            ping_req->error = strdup("ping arguments invalid");
            return ping_req;
        }
    } else {
        int k = sscanf(oam_command, " %s %s %d%n",
                       start_name, ping_req->mep_stop, &ping_req->level, &l);
        if (k < 3) {
            ping_req->error = strdup("ping arguments invalid");
            return ping_req;
        }
        iface_name[0] = 0;
    }

    ping_req->mep_start = find_mep_start(start_name);
    if (ping_req->mep_start == NULL) {
        ping_req->error = strdup_printf("ping start '%s' invalid", start_name);
        return ping_req;
    }

    if (!parse_ping_returnif(ping_req, iface_name)) {
        return ping_req;
    }

    if (!parse_ping_options(ping_req, oam_command+l, allow_num)) {
        //TODO add something to the error?
    }

    return ping_req;
}

// always returns a request, sets ret->error to an error message
struct oam_request *parse_rping_command(const char *oam_command,
        struct command_connection *conn)
{
    int l;
    char start_name[32];
    char iface_name[32];

    struct oam_request *rping_req = new_oam_request("rping", conn);

    if (oam_command[0]=='@') {
        int k = sscanf(oam_command, "@%s %s %s %d%n",
                       iface_name, start_name, rping_req->mep_stop, &rping_req->level, &l);
        if (k < 4) {
            rping_req->error = strdup("rping arguments invalid");
            return rping_req;
        }
    } else {
        int k = sscanf(oam_command, " %s %s %d%n",
                       start_name, rping_req->mep_stop, &rping_req->level, &l);
        if (k < 3) {
            rping_req->error = strdup("rping arguments invalid");
            return rping_req;
        }
        iface_name[0] = 0;
    }

    rping_req->mep_start = find_mep_start(start_name);
    if (rping_req->mep_start == NULL) {
        rping_req->error = strdup_printf("rping start '%s' invalid", start_name);
        return rping_req;
    }

    if (!parse_ping_returnif(rping_req, iface_name)) {
        return rping_req;
    }

    while (isspace(oam_command[l])) l++;
    rping_req->remote_command = strdup(oam_command+l);

    return rping_req;
}

// always returns a request, sets ret->error to an error message
struct oam_request *parse_rlist_command(const char *oam_command,
        struct command_connection *conn)
{
    int l;
    char start_name[32];
    char iface_name[32];

    struct oam_request *rlist_req = new_oam_request("rlist", conn);

    if (oam_command[0]=='@') {
        int k = sscanf(oam_command, "@%s %s %s %d%n",
                       iface_name, start_name, rlist_req->mep_stop, &rlist_req->level, &l);
        if (k < 4) {
            rlist_req->error = strdup("rlist arguments invalid");
            return rlist_req;
        }
    } else {
        int k = sscanf(oam_command, " %s %s %d%n",
                       start_name, rlist_req->mep_stop, &rlist_req->level, &l);
        if (k < 3) {
            rlist_req->error = strdup("rlist arguments invalid");
            return rlist_req;
        }
        iface_name[0] = 0;
    }

    rlist_req->mep_start = find_mep_start(start_name);
    if (rlist_req->mep_start == NULL) {
        rlist_req->error = strdup_printf("rlist start '%s' invalid", start_name);
        return rlist_req;
    }

    if (!parse_ping_returnif(rlist_req, iface_name)) {
        return rlist_req;
    }

    while (isspace(oam_command[l])) l++;
    if (oam_command[l]) {
        rlist_req->error = strdup("rlist doesn't take so many arguments");
        return rlist_req;
    }

    return rlist_req;
}

static const char help_str[] =
    "Available commands:\n"
    "help - get help\n"
    "exit - exit OAM\n"
    "mode <mode> - terminal mode. Mode can be 'dump' or 'json'.\n"
    "log [module newlevel] - get current log levels or set it for the given module.\n"
    "list - list monitoring start points\n"
    "rlist[@if] <stream:mep-start/mip> <mep-stop/mip/any> <level> - list monitoring start points of the remote node.\n"
    "sessions [stream] - list active sessions for 'stream'. List all sessions if no 'stream' specified.\n"
    "stop [stream session_id] - stop a running OAM session, identified by stream:session_id. Stops the last session if no parameters given.\n"
    "returns - list return interfaces\n"
    "ping[@if] <stream:mep-start/mip> <mep-stop/mip/any> <level> [-r] [-o] [-i <interval>] [-n <count>] [-t <ttl>]\n"
    "rping[@if] <stream:mep-start/mip> <mep-stop/mip> <level> <remote stream:mep-start/mip> <remote mep-stop/mip/any> <remote level> [-r] [-o] [-i <interval>] [-n <count>] [-t <ttl>]\n";

static int list_mep_cb(const char *key, void *value, void *userdata)
{
    (void)key;
    struct MepStart *start = value;
    FILE *cmd_w = userdata;
    print_mep_start(start, cmd_w);
    return 1;
}

static int list_oam_ifaces_cb(const char *ifname, void *value, void *userdata)
{
    struct Interface *iface = value;
    FILE *cmd_w = userdata;

    const char *return_ip = oamif_get_ip(iface);
    unsigned return_port = oamif_get_port(iface);
    fprintf(cmd_w, "%s ip %s port %u",
            ifname, return_ip, return_port);
    if (iface == get_default_oam_interface()) {
        fprintf(cmd_w, " (default, node id %u)\n", oamif_get_uid(iface));
    } else {
        fprintf(cmd_w, "\n");
    }

    return 1;
}

static int list_log_modules_cb(const char *mod_name, LOGGING_LEVELS current_level, void *userdata)
{
    FILE *cmd_w = userdata;
    fprintf(cmd_w, "  %s level %s\n", mod_name, log_string_from_level(current_level));
    return 1;
}

#define TELNET_IAC         0xff /* Interpret As Command */
#define TELNET_INTERRUPT   0xf4 /* interrupt process */
#define TELNET_WILL        0xfb
#define TELNET_WONT        0xfc
#define TELNET_DO          0xfd
#define TELNET_DONT        0xfe
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
                        char reply[] = {TELNET_IAC, TELNET_WILL, TELNET_TIMING_MARK, 0};
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

static void command_loop(struct command_connection *conn)
{
#define ERROR(msg, ...)                             \
    fprintf(cmd_w, "Error: " msg "\n",              \
            ##__VA_ARGS__);                         \
    continue

#define CHECK_REQUEST(req)                                      \
    if (req->error) {                                           \
        fprintf(cmd_w, "Error: %s command is invalid: %s\n",    \
            req->type, req->error);                             \
        delete_oam_request(req);                                \
        continue;                                               \
    }


    int cmd_fd = conn->socket_fd;
    FILE *cmd_w = conn->cmd_w;

    char oam_command[255], last_command[255];
    char streamname[32];

    const char *last_stream=NULL; // the stream name of the last issued command

    if (have_default_iface()) {
        fprintf(cmd_w, "OAM ready.\n");
    } else {
        fprintf(cmd_w, "OAM ready, but has no configured return interface.\n");
    }

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
            else if(strcmp(oam_command, "help") == 0){
                fprintf(cmd_w, help_str);
            }
            else if (strcmp(oam_command, "list") == 0) {
                fprintf(cmd_w, "Available MEP Start points:\n");
                foreach_mep_start(list_mep_cb, conn->cmd_w);
            }
            else if (strncmp(oam_command, "log", 3) == 0) {
                char modulename[64];
                char newlevel[16];
                int k = sscanf(oam_command, "log %s %s", modulename, newlevel);
                if (k == 0 || k == EOF) {
                    fprintf(cmd_w, "Logging modules:\n");
                    log_get_levels(list_log_modules_cb, cmd_w);
                } else if (k == 2) {
                    int nlvl = log_level_from_string(newlevel);
                    if (nlvl < 0) {
                        fprintf(cmd_w, "Log level '%s' invalid.\n", newlevel);
                    } else {
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
            else if (strncmp(oam_command, "sessions", 8) == 0) {
                int k=sscanf(oam_command, "sessions %s", streamname);
                if(k==0 || k==EOF){
                    list_sessions_of_all_streams(conn->cmd_w);
                }
                else if (k==1) {
                    struct StreamSessions *stream = get_stream_sessions(streamname);
                    if (stream == NULL) {
                        fprintf(cmd_w, "Invalid stream name '%s'.\n", streamname);
                    } else {
                        fprintf(cmd_w, "Stream %s sessions:\n", streamname);
                        list_sessions_of_stream(stream, conn->cmd_w);
                    }
                }
                else {
                    fprintf(cmd_w, "Invalid parameters for 'sessions' command.\n");
                }
            }
            else if (strncmp(oam_command, "stop", 4) == 0) {
                int session;
                int k=sscanf(oam_command, "stop %s %d", streamname, &session);
                if(k==0 || k==EOF){
                    if(last_stream == NULL)
                        fprintf(cmd_w,"No previous command to stop.\n");
                    else
                        stop_session(last_stream, -1, conn);
                } else if(k==2){
                    stop_session(streamname, session, conn);
                } else
                    fprintf(cmd_w,"invalid parameters for stop.\n");
            }
            else if (strcmp(oam_command, "returns") == 0) {
                fprintf(cmd_w, "Available OAM return interfaces:\n");
                foreach_oam_ifaces(list_oam_ifaces_cb, conn->cmd_w);
            }
            else if (strncmp(oam_command, "ping", 4) == 0) {
                struct oam_request *ping_req = parse_ping_command(oam_command+4, true, true, conn);
                CHECK_REQUEST(ping_req);
                const char *req_stream = ping_req->mep_start->stream_name;
                if (!initiate_request(ping_req)) {
                    ERROR("sending ping command failed");
                }
                last_stream = req_stream;
            }
            else if (strncmp(oam_command, "rping", 5) == 0) {
                struct oam_request *rping_req = parse_rping_command(oam_command+5, conn);
                CHECK_REQUEST(rping_req);
                if (!initiate_request(rping_req)) {
                    ERROR("sending rping command failed");
                }
            }
            else if (strncmp(oam_command, "rlist", 5) == 0) {
                struct oam_request *rlist_req = parse_rlist_command(oam_command+5, conn);
                CHECK_REQUEST(rlist_req);
                if (!initiate_request(rlist_req)) {
                    ERROR("sending rlist command failed");
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
            break;
        }
    }
    log_info("Telnet closed");

#undef ERROR
#undef CHECK_REQUEST
}

static void *command_thread(void *arg)
{
    struct command_connection *conn = arg;
    command_loop(conn);
    struct Thread *thread = conn->thread;
    hashmap_remove(command_connections, conn->name);
    thread_exit(thread);
    return NULL;
}

void oam_start_command_connection(int fd)
{
    struct command_connection *conn = calloc_struct(command_connection);
    conn->socket_fd = fd;
    // note: inverse operation is fd=fileno(file)
    conn->cmd_w = fdopen(fd, "w");
    //TODO if we want to fread() we need to duplicate the handle
    //int cmd_fd_dup = dup(cmd_fd);
    //FILE *cmd_r = fdopen(cmd_fd_dup, "r");

    setvbuf(conn->cmd_w, NULL, _IOLBF, 0);

    conn->thread = thread_launch(command_thread, conn, "command");
    if (conn->thread == NULL) {
        log_perror("could not create new oam thread");
        fclose(conn->cmd_w); // we only need to close the FILE*
        free(conn);
        return;
    }
    conn->name = strdup_printf("conn %u", thread_getid(conn->thread));
    hashmap_insert(command_connections, conn->name, conn);
}

static int alert_cb(const char *key, void *value, void *userdata)
{
    (void)key;
    struct command_connection *conn = value;
    char *msg = userdata;
    FILE *cmd_w = command_connection_get_w(conn);
    if (cmd_w) fprintf(cmd_w, "\n\n%s\n\n", msg);
    command_connection_release_w(conn);
    return 1;
}

void oam_cli_alert(const char *fmt, ...)
{
    char msg[200];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    hashmap_foreach(command_connections, alert_cb, msg);
}

static int command_connection_delete_cb(const char *key, void *value, void *userdata)
{
    (void)key; // same as conn->name
    (void)userdata;
    struct command_connection *conn = value;
    //TODO while (conn->w_users > 0) {wait}
    stop_all_sessions_of_connection(conn);
    fclose(conn->cmd_w); // we only need to close the FILE*
    free(conn->name);
    free(conn);
    return 1;
}

void init_cmd_module(void)
{
    command_connections = new_hashmap(7, command_connection_delete_cb, NULL);
}

void finish_cmd_module(void)
{
    delete_hashmap(command_connections);
}

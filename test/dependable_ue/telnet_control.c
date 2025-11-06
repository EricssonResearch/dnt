/*
*    UE control via Telnet interface v0.1
*
*    compile: gcc telnet_control.c  -o telnet_control
*
*/
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <poll.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8000
#define TIMEOUT_MS 1000
#define LOOP_DELAY_SEC 2

#define PASS_THRESHOLD 15
#define TOTAL_PKT_PER_PERIOD 16

static volatile sig_atomic_t stop_flag = 0;
static int global_sock = -1;

/* ---------- Signal Handling ---------- */
void handle_sigint(int sig) {
    (void)sig;
    stop_flag = 1;
    if (global_sock >= 0) {
        const char *exit_cmd = "exit\r\n";
        send(global_sock, exit_cmd, strlen(exit_cmd), 0);
    }
}

/* ---------- Helper: receive with timeout ---------- */
ssize_t recv_with_timeout(int sock, char *buf, size_t bufsize, int timeout_ms) {
    struct pollfd pfd = { .fd = sock, .events = POLLIN };
    ssize_t total = 0;

    while (1) {
        int ret = poll(&pfd, 1, timeout_ms);
        if (ret <= 0)
            break; // timeout or error

        ssize_t n = recv(sock, buf + total, bufsize - 1 - total, 0);
        if (n <= 0)
            break;
        total += n;

        // short delay to let multi-line responses arrive
        if (n < (ssize_t)(bufsize - 1 - total))
            usleep(100 * 1000); // 100 ms
    }
    buf[total] = '\0';
    return total;
}

/* ---------- Data Structures ---------- */
typedef struct {
    long data_packets;
} pw_stats_t;

typedef struct {
    long passed;
    long discarded;
} compound_stats_t;

typedef struct {
    int pw_id;
    long delta_packets;
} table_entry_t;

/* ---------- Ping and Parsing ---------- */
int send_and_parse_ping(int sock, pw_stats_t *pw_out, compound_stats_t *comp_out) {
    const char *cmd = "ping compound4_L4_pre-prf1_4 any 4 -o\r\n";
    if (send(sock, cmd, strlen(cmd), 0) < 0) {
        perror("send");
        return -1;
    }

    char buf[32768];
    ssize_t n = recv_with_timeout(sock, buf, sizeof(buf), TIMEOUT_MS);
    if (n <= 0) {
        memset(pw_out, 0, 3 * sizeof(pw_stats_t));
        memset(comp_out, 0, sizeof(compound_stats_t));
        return -1;
    }

    // Initialize outputs
    for (int i = 0; i < 3; i++)
        pw_out[i].data_packets = -1;
    comp_out->passed = comp_out->discarded = -1;

    const char *targets[3] = {
        "pw41_L4_pre-pef2_4",
        "pw42_L4_pre-pef2_4",
        "pw43_L4_pre-pef2_4"
    };

    // parse PW stats: "stats: data packets <n>"
    for (int i = 0; i < 3; i++) {
        const char *p = strstr(buf, targets[i]);
        if (!p) continue;
        const char *s = strstr(p, "stats: data packets");
        if (s) {
            long val;
            if (sscanf(s, "stats: data packets %ld", &val) == 1)
                pw_out[i].data_packets = val;
        }
    }

    // parse compound object passed/discarded
    const char *c = strstr(buf, "compound_L4_post-pef2_4");
    if (c) {
        const char *p = strstr(c, "passed");
        const char *d = strstr(c, "discarded");
        if (p) sscanf(p, "passed %ld", &comp_out->passed);
        if (d) sscanf(d, "discarded %ld", &comp_out->discarded);
    }

    return 0;
}

/* ---------- Mask/Unmask Command ---------- */
int send_mask_command(int sock, const char *action, const char *pipeline) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "%s %s\r\n", action, pipeline);

    if (send(sock, cmd, strlen(cmd), 0) < 0) {
        perror("send");
        return -1;
    }

    char buf[4096];
    ssize_t n = recv(sock, buf, sizeof(buf), 0);
//    ssize_t n = recv_with_timeout(sock, buf, sizeof(buf), 10);
    if (n <= 0) {
        fprintf(stderr, "%s %s: no reply or timeout\n", action, pipeline);
        return -1;
    }

    if (strstr(buf, "Error:")) {
        fprintf(stderr, "%s %s failed: %s\n", action, pipeline, buf);
        return -1;
    }

    if (strstr(buf, "masked") || strstr(buf, "unmasked")) {
        //printf("%s %s succeeded: %s\n", action, pipeline, buf);
        return 0;
    }

    fprintf(stderr, "%s %s: unexpected response: %s\n", action, pipeline, buf);
    return -1;
}

/* ---------- Main ---------- */
int main(void) {
    int sock;
    struct sockaddr_in server_addr;

    signal(SIGINT, handle_sigint);  // install Ctrl-C handler

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }
    global_sock = sock;

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    printf("Connecting to %s:%d...\n", SERVER_IP, SERVER_PORT);
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sock);
        return 1;
    }

    printf("Connected. Press Ctrl-C to exit.\n");

    pw_stats_t pw_prev[3] = {{0}};
    compound_stats_t comp_prev = {0};
    int first_cycle = 1;

    while (!stop_flag) {
        pw_stats_t pw[3];
        compound_stats_t comp;

        if (send_and_parse_ping(sock, pw, &comp) < 0) {
            fprintf(stderr, "No reply or connection issue.\n");
            sleep(LOOP_DELAY_SEC);
            continue;
        }

        // Prepare table with deltas
        table_entry_t table[3];
        for (int i = 0; i < 3; i++) {
            long delta = 0;
            if (!first_cycle && pw_prev[i].data_packets >= 0)
                delta = pw[i].data_packets - pw_prev[i].data_packets;
            if (delta < 0) delta = 0;
            table[i] = (table_entry_t){ i + 1, delta };
            pw_prev[i] = pw[i];
        }

        // Sort table by descending Δpackets
        for (int i = 0; i < 2; i++) {
            for (int j = i + 1; j < 3; j++) {
                if (table[j].delta_packets > table[i].delta_packets) {
                    table_entry_t tmp = table[i];
                    table[i] = table[j];
                    table[j] = tmp;
                }
            }
        }

        // Compute compound deltas
        long delta_passed = 0, delta_discarded = 0;
        if (!first_cycle) {
            delta_passed = comp.passed - comp_prev.passed;
            delta_discarded = comp.discarded - comp_prev.discarded;
            if (delta_passed < 0) delta_passed = 0;
            if (delta_discarded < 0) delta_discarded = 0;
        }
        comp_prev = comp;

        // do mask control, show meas results
        printf("Compound Δpassed = %ld, Δdiscarded = %ld\n",
               delta_passed, delta_discarded);
        printf("%-8s %-13s %-8s %-9s \n", "PW", "ΔPackets", "p_drop", "Status");
        printf("----------------------\n");

        double p_combined_drop = 1;
        bool mask = false;
        for (int i = 0; i < 3; i++) {

            char member[32];
            sprintf(member, "prf4-member%d", table[i].pw_id);

            if(!mask){ // the first should always be unmasked
                send_mask_command(sock, "unmask",  member);
                if(delta_passed != 0) {
                    if(delta_passed >= table[i].delta_packets)
                        p_combined_drop = p_combined_drop * (1.0-(double)table[i].delta_packets/delta_passed);
                    // else p_combined_drop * 1
                } else {
                    if(table[i].delta_packets > 0)
                        p_combined_drop = p_combined_drop * (1.0-(double)table[i].delta_packets/TOTAL_PKT_PER_PERIOD);
                }
            }
            else
                send_mask_command(sock, "mask",  member);

            printf("pw%-6d %-12ld %f %s \n", table[i].pw_id, table[i].delta_packets, p_combined_drop, mask?"mask":"unmask");

            if((1.0-p_combined_drop) > 0.98) {
                mask = true;
                printf("*");
            }
        }
        printf("--------------------->\n\n");

        first_cycle = 0;

        // Sleep while allowing Ctrl-C
        for (int i = 0; i < LOOP_DELAY_SEC * 10 && !stop_flag; i++)
            usleep(100000);
    }

    printf("\nExiting gracefully...\n");
    if (global_sock >= 0) {
        const char *exit_cmd = "exit\r\n";
        send(global_sock, exit_cmd, strlen(exit_cmd), 0);
        close(global_sock);
        global_sock = -1;
    }
    printf("Connection closed.\n");

    return 0;
}

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

#include "json.h"

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8000
#define TIMEOUT_MS 1500
#define LOOP_DELAY_SEC 2

#define PASS_THRESHOLD 15
#define TOTAL_PKT_PER_PERIOD 20

#define COMPOUND_MEP_START    "compound4_L4_pre-prf1_4"
#define COMPOUND_MEP_STOP     "compound_L4_post-pef2_4"
#define MIP_ARRAY_INIT \
{ \
    "pw41_L4_pre-pef2_4", \
    "pw42_L4_pre-pef2_4", \
    "pw43_L4_pre-pef2_4" \
}

static volatile sig_atomic_t stop_flag = 0;
static int global_sock = -1;

/* ---------- Signal Handling ---------- */
static void handle_sigint(int sig) {
    (void)sig;
    stop_flag = 1;
}

/* ---------- Helper: receive with timeout ---------- */
static ssize_t recv_with_timeout(int sock, char *buf, size_t bufsize, char sep, int timeout_ms)
{
    struct pollfd pfd = { .fd = sock, .events = POLLIN };
    ssize_t total = 0;
    total = sprintf(buf, "{\"responses\": [");

    int skip_first_line = 1;

    while (1) {
        int ret = poll(&pfd, 1, timeout_ms);
        if (ret <= 0)
            break;

        char tmp[4096];
        ssize_t n = recv(sock, tmp, sizeof(tmp), 0);
        if (n <= 0)
            break;

        char *data = tmp;
        ssize_t len = n;

        /* --- Skip the first line starting with 'OAM request ping session ...' --- */
        if (skip_first_line) {
            char *nl = (char*)memchr(tmp, '\n', n);
            if (nl) {
                data = nl + 1;
                len  = n - (data - tmp);
                skip_first_line = 0;
                if (len == 0)
                    continue;
            } else {
                // still in first line → skip chunk entirely
                continue;
            }
        }

        /* --- Add ',' before '\n' to separate json array elements before writing into buf --- */
        for (ssize_t i = 0; i < len; i++) {
            char c = data[i];
            if (c == '\n')
                buf[total++] = sep;   // add ',' before newline

            if (total + 2 >= (ssize_t)bufsize)  // ensure space
                goto finish;

            buf[total++] = c;
        }

        // small delay
        if (n < (ssize_t)sizeof(tmp))
            usleep(100 * 1000);
    }

finish:
    /* ensure we have room for "]}\0", just to compile */
    if (total + 1 >= (ssize_t)bufsize)
        total = bufsize - 1;

    buf[total-2] = ']';
    buf[total-1] = '}';
    buf[total] = '\0';
    return total;
}

/* ---------- Data Structures ---------- */
typedef struct {
    long data_packets;
    long data_octets;
    long oam_recv;
    long oam_sent;
} pw_stats_t;

typedef struct {
    long passed;
    long discarded;
    long data_packets;
    long data_octets;
    long oam_recv;
    long oam_sent;
} compound_stats_t;

typedef struct {
    int pw_id;
    long delta_data_packets;
    long delta_oam_packets;
    long delta_rec_passed;
    long delta_rec_discarded;
} table_entry_t;

static int send_and_parse_ping_json(int sock, pw_stats_t *pw_out, compound_stats_t *comp_out) {
    #define THROW(msg, ...)                     \
        do {                                    \
            printf(msg, ##__VA_ARGS__);         \
            json_delete(js);                    \
            return -1;                          \
        } while (0)

    #define JS_OBJECT_GET(_json, _key, _type)                                   \
        struct JsonValue *_json##_key = json_object_get_##_type(_json, #_key);  \
        if (_json##_key == NULL) {                                              \
            THROW("No " #_key " in reply message.\n");                          \
        }

    const char *cmd = "ping " COMPOUND_MEP_START " any 4 -o\r\n";
    if (send(sock, cmd, strlen(cmd), 0) < 0) {
        perror("send");
        return -1;
    }

    char buf[32768];
    ssize_t n = recv_with_timeout(sock, buf, sizeof(buf), ',', TIMEOUT_MS);
    if (n <= 0) {
        memset(pw_out, 0, 3 * sizeof(pw_stats_t));
        memset(comp_out, 0, sizeof(compound_stats_t));
        printf("recv timed out!\n");
        return -1;
    }

    // Initialize outputs
    for (int i = 0; i < 3; i++){
        pw_out[i].data_packets = -1;
        pw_out[i].data_octets = 0;
        pw_out[i].oam_recv = -1;
        pw_out[i].oam_sent = -1;
    }

    comp_out->passed = comp_out->discarded = -1;
    comp_out->data_packets = comp_out->data_octets = 0;
    comp_out->oam_recv = comp_out->oam_sent = -1;

    const char *targets[3] = MIP_ARRAY_INIT;

    char *jerr;
    struct JsonValue *js = json_parse(buf, strlen(buf), &jerr);
    if (js == NULL || js->type != JSON_OBJECT) {
        printf("JSON in reply is invalid: %s\n%s\n", jerr, buf);
        free(jerr);
        return -1;
    }
    JS_OBJECT_GET(js, responses, array);

    printf("Responses from: ");
    // parse PW stats
    for (int i = 0; i < 3; i++) {
        // loop on answers
        for (unsigned j=0; j<json_array_size(jsresponses); j++) {
            struct JsonValue *ja = json_array_at(jsresponses, j);
            if (ja->type != JSON_OBJECT) {
                printf("list result is not json object.");
            }
            JS_OBJECT_GET(ja, receiver, object);
            JS_OBJECT_GET(jareceiver, name, string);

            if(strcmp(targets[i], jareceivername->v.string) != 0)
                continue;

            printf("%s ", targets[i]);

            // found it
            JS_OBJECT_GET(jareceiver, data_packets, number);
            JS_OBJECT_GET(jareceiver, data_octets, number);
            JS_OBJECT_GET(jareceiver, oam_recv, number);
            JS_OBJECT_GET(jareceiver, oam_send, number);

            pw_out[i].data_packets = jareceiverdata_packets->v.number;
            pw_out[i].data_octets = jareceiverdata_octets->v.number;
            pw_out[i].oam_recv = jareceiveroam_recv->v.number;
            pw_out[i].oam_sent = jareceiveroam_send->v.number;
        }
    }
    printf("\n");

    // loop on answers again, for compound
    for (unsigned j=0; j<json_array_size(jsresponses); j++) {
        struct JsonValue *ja = json_array_at(jsresponses, j);
        if (ja->type != JSON_OBJECT) {
            printf("list result is not json object.");
        }
        JS_OBJECT_GET(ja, receiver, object);
        JS_OBJECT_GET(jareceiver, name, string);

        if(strcmp(COMPOUND_MEP_STOP, jareceivername->v.string) != 0)
            continue;

        // found it
        JS_OBJECT_GET(jareceiver, data_packets, number);
        JS_OBJECT_GET(jareceiver, data_octets, number);
        JS_OBJECT_GET(jareceiver, oam_recv, number);
        JS_OBJECT_GET(jareceiver, oam_send, number);

        comp_out->data_packets = jareceiverdata_packets->v.number;
        comp_out->data_octets = jareceiverdata_octets->v.number;
        comp_out->oam_recv = jareceiveroam_recv->v.number;
        comp_out->oam_sent = jareceiveroam_send->v.number;

        JS_OBJECT_GET(jareceiver, object, object);
        JS_OBJECT_GET(jareceiverobject, passed_packets, number);
        JS_OBJECT_GET(jareceiverobject, discarded_packets, number);
        comp_out->passed = jareceiverobjectpassed_packets->v.number;
        comp_out->discarded = jareceiverobjectdiscarded_packets->v.number;
    }

    return 0;
}

static void calculate_deltas(
        const pw_stats_t *pw, const pw_stats_t *pw_prev,
        const compound_stats_t *comp, const compound_stats_t *comp_prev,
        table_entry_t *table, table_entry_t *compound_deltas,
        int first_cycle)
{
    // calculate compound deltas first
    long d_data      = 0;
    long d_oam       = 0;
    long d_passed    = 0;
    long d_discarded = 0;

    if (!first_cycle) {
        d_passed    = comp->passed    - comp_prev->passed;
        d_discarded = comp->discarded - comp_prev->discarded;
        d_data      = comp->data_packets - comp_prev->data_packets;
        d_oam       = comp->oam_recv - comp_prev->oam_recv;
        if (d_passed < 0) d_passed = 0;
        if (d_discarded < 0) d_discarded = 0;
        if (d_data < 0) d_data = 0;
        if (d_oam < 0) d_oam = 0;

    }
    compound_deltas->delta_rec_passed    = d_passed;
    compound_deltas->delta_rec_discarded = d_discarded;
    compound_deltas->delta_data_packets  = d_data;
    compound_deltas->delta_oam_packets   = d_oam;

    for (int i = 0; i < 3; i++) {

        long d_data_packets = 0;
        long d_oam_packets  = 0;

        if (!first_cycle) {
            if (pw_prev[i].data_packets >= 0)
                d_data_packets = pw[i].data_packets - pw_prev[i].data_packets;

            if (pw_prev[i].oam_recv >= 0)
                d_oam_packets = pw[i].oam_recv - pw_prev[i].oam_recv;

            if (d_data_packets < 0) d_data_packets = 0;
            if (d_oam_packets < 0) d_oam_packets = 0;
        }

        table[i].pw_id               = i + 1;
        table[i].delta_data_packets  = d_data_packets;
        table[i].delta_oam_packets   = d_oam_packets;
        table[i].delta_rec_passed    = d_passed;
        table[i].delta_rec_discarded = d_discarded;
    }
}

static int compare_table_entries(const void *a, const void *b)
{
    const table_entry_t *A = (const table_entry_t *)a;
    const table_entry_t *B = (const table_entry_t *)b;

//    if (B->delta_data_packets > A->delta_data_packets) return 1;
//    if (B->delta_data_packets < A->delta_data_packets) return -1;
    if (B->delta_oam_packets > A->delta_oam_packets) return 1;
    if (B->delta_oam_packets < A->delta_oam_packets) return -1;
    return 0;
}

static void sort_table(table_entry_t *table)
{
    qsort(table, 3, sizeof(table_entry_t), compare_table_entries);
}

/* ---------- Mask/Unmask Command ---------- */
static int send_mask_command(int sock, const char *action, const char *pipeline) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "%s %s\r\n", action, pipeline);

    if (send(sock, cmd, strlen(cmd), 0) < 0) {
        perror("send");
        return -1;
    }

    char buf[4096];
    ssize_t n = recv(sock, buf, sizeof(buf), 0);
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

/* ---------- Mode json command ---------- */
static int set_mode_json(int sock) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "mode json\r\n");

    if (send(sock, cmd, strlen(cmd), 0) < 0) {
        perror("send");
        return -1;
    }

    char buf[4096];
    ssize_t n = recv(sock, buf, sizeof(buf), 0);
    if (n <= 0) {
        fprintf(stderr, "mode json: no reply or timeout\n");
        return -1;
    }

    if (!strstr(buf, "is json")) {
        fprintf(stderr, "mode json failed: %s\n", buf);
        return -1;
    }
    else printf("Mode set to json.\n");

    return 0;
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

    // Read welcome message.
    char buf[4096];
    recv(sock, buf, sizeof(buf), 0);

    printf("Connected. Press Ctrl-C to exit.\n");

    usleep(200 * 1000);         // wait a little bit...
    set_mode_json(sock);

    pw_stats_t pw_prev[3] = {{0}};
    compound_stats_t comp_prev = {0};
    int first_cycle = 1;

    while (!stop_flag) {
        pw_stats_t pw[3];
        compound_stats_t comp;
        table_entry_t table[3];
        table_entry_t compound_deltas;

        if (send_and_parse_ping_json(sock, pw, &comp) < 0) {
            fprintf(stderr, "No reply or connection issue.\n");
            sleep(LOOP_DELAY_SEC);
            continue;
        }

        printf("Measured Data [ %ld %ld %ld ] OAM [ %ld %ld %ld ]\n",
            pw[0].data_packets, pw[1].data_packets, pw[2].data_packets,
            pw[0].oam_recv, pw[1].oam_recv, pw[2].oam_recv);

        // Prepare table with deltas
        calculate_deltas(pw, pw_prev, &comp, &comp_prev, table, &compound_deltas, first_cycle);

        // remember previous values
        comp_prev = comp;
        for (int i = 0; i < 3; i++)
            pw_prev[i] = pw[i];

        // Sort table by descending Δpackets
        sort_table(table);

        // delta to be used as reference. Set to delta_data_packets when control is based on data packets.
        long delta_packets = compound_deltas.delta_oam_packets; // after elimination

        // do mask control, show measurement results
        printf("Compound:  Δpassed = %ld, Δdiscarded = %ld ΔData = %ld ΔOAM = %ld\n",
               compound_deltas.delta_rec_passed,
               compound_deltas.delta_rec_discarded,
               compound_deltas.delta_data_packets,
               compound_deltas.delta_oam_packets);

        printf("%-8s %-13s %-13s %-13s %-13s %-9s %-8s\n",
               "PW",
               "ΔDataPkts",
               "ΔOAMpkts",
               "ΔRecPass",
               "ΔRecDrop",
               "p_drop",
               "Status");

        printf("-------------------------------------------------------------------------------\n");

        double p_combined_drop = 1.0;
        bool mask = false;

        for (int i = 0; i < 3; i++) {

            char member[32];
            sprintf(member, "prf4-member%d", table[i].pw_id);

            // Mask / unmask logic
            if (!mask) {   // first path stays unmasked
                send_mask_command(sock, "unmask", member);

                if (delta_packets != 0) {
                    if (delta_packets >= table[i].delta_oam_packets)
                        p_combined_drop *= (1.0 -
                            (double)table[i].delta_oam_packets / delta_packets);
                } else {
                    if (table[i].delta_oam_packets > 0)
                        p_combined_drop *= (1.0 -
                            (double)table[i].delta_oam_packets / TOTAL_PKT_PER_PERIOD);
                }
            } else {
                send_mask_command(sock, "mask", member);
            }

            // Print row with all deltas
            printf("pw%-6d %-13ld %-13ld %-13ld %-13ld %-9f %-8s\n",
                   table[i].pw_id,
                   table[i].delta_data_packets,
                   table[i].delta_oam_packets,
                   table[i].delta_rec_passed,
                   table[i].delta_rec_discarded,
                   (1.0 - (double)table[i].delta_oam_packets / delta_packets),
                   //p_combined_drop,
                   mask ? "mask" : "unmask");

            // After this PW, check failure threshold
            if ( (i >= 0) && (1.0 - p_combined_drop) > 0.98) {     // i>=0: 1 path, i>=1: 2 paths active
                mask = true;
            }
        }

        printf("-------------------------------------------------------------------------------\n\n");

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

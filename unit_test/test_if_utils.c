
#include "testing.h"

#include "if_utils.h"
#include "interface.h"
#include "packet.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <sys/socket.h>


TEST_INIT("Interface Utils");

// XXX stubs for stuff that we transitively depend on but don't need

//TODO do we want to test monitor_error_queue() ?
struct Thread *thread_launch(void* (*thread_fn)(void *), void *thread_arg, const char *name, ...);
struct Thread *thread_launch(void* (*thread_fn)(void *), void *thread_arg, const char *name, ...)
    { (void)thread_fn; (void)thread_arg; (void)name; return NULL; }
struct Thread *thread_stop(struct Thread *thread);
struct Thread *thread_stop(struct Thread *thread)
    { (void)thread; return NULL; }

//TODO do we want to test iface_common_process() ?
struct ParseTree;
struct PipelineIterator *parsetree_identify(struct ParseTree *pt, struct Packet *p);
struct PipelineIterator *parsetree_identify(struct ParseTree *pt, struct Packet *p)
    { (void)pt; (void)p; return NULL; }
void pipe_iterator_run(struct PipelineIterator *pi);
void pipe_iterator_run(struct PipelineIterator *pi)
    { (void)pi; }
bool notification_push_event(const char *source, NotificationLevel level, struct JsonValue *message)
    { (void)source; (void)level; (void)message; return true; }
struct ParseTree *new_parsetree(const struct Interface *iface);
struct ParseTree *new_parsetree(const struct Interface *iface)
    { (void)iface; return NULL; }

// XXX end stubs


static void test_common_send(void)
{
    int socks[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, socks) < 0) {
        SKIP("can't create sockets: %s", strerror(errno));
    }

    // a minimal interface
    struct Interface iface;
    memset(&iface, 0, sizeof(iface));
    iface.name = strdup("dummy for testing");
    iface.state = IFSTATE_OPEN;

    struct Packet *p = new_packet(&iface);
    OK_FATAL(p != NULL, "have packet");
    OK_FATAL(p->buf != NULL, "have packet buffer");

    // fill with some data
    for (unsigned i=0; i<PACKET_BUF_LEN; i++) {
        p->buf[i] = i % 256;
    }
    p->len = 256;

    // a fairly normal proto stack
    OK(packet_identify_header(p, PROTO_ID_ETH, 0, 14) == true, "eth");
    OK(packet_identify_header(p, PROTO_ID_CVLAN, 14, 4) == true, "cvlan");
    OK(packet_identify_header(p, PROTO_ID_IPv6, 18, 40) == true, "ipv6");
    OK(packet_identify_header(p, PROTO_ID_PAYLOAD, 58, 198) == true, "payload");
    OK(p->header_count == 4, "header_count %u", p->header_count);

    OK(iface_common_send(&iface, p, socks[0], NULL, 0) == true, "could not send");

    unsigned char recvbuf[1024];
    int r;

    r = recv(socks[1], recvbuf, sizeof(recvbuf), 0);
    OK(r > 0, "recv error: %s", strerror(errno));
    OK(r == 14+4+40+198, "recv length %d", r);

    // validate the buffer contents
    unsigned ru = 0;
    for (unsigned i=0; i<p->header_count; i++) {
        for (unsigned j=0; j<p->headers[i].len; j++) {
            OK(recvbuf[ru] == p->buf[p->headers[i].start+j], "byte %u differs %u %u", ru,
                    recvbuf[ru], p->buf[p->headers[i].start+j]);
            ru++;
        }
    }

    packet_add_header(p, 4, PROTO_ID_ARP, 10); // end
    packet_add_header(p, 0, PROTO_ID_IPv4, 20); // beginning
    packet_add_header(p, 3, PROTO_ID_SVLAN, 4); // middle
    OK(p->header_count == 7, "header_count %u", p->header_count);
    packet_del_header(p, 2); // CVLAN
    OK(p->header_count == 6, "header_count %u", p->header_count);

    OK(iface_common_send(&iface, p, socks[0], NULL, 0) == true, "could not send");

    r = recv(socks[1], recvbuf, sizeof(recvbuf), 0);
    OK(r > 0, "recv error: %s", strerror(errno));
    OK(r == 14+40+198+10+20+4, "recv length %d", r);

    // validate the buffer contents
    ru = 0;
    for (unsigned i=0; i<p->header_count; i++) {
        for (unsigned j=0; j<p->headers[i].len; j++) {
            OK(recvbuf[ru] == p->buf[p->headers[i].start+j], "byte %u differs %u %u", ru,
                    recvbuf[ru], p->buf[p->headers[i].start+j]);
            ru++;
        }
    }

    OK(delete_packet(p) == NULL, "always returns null");
    close(socks[0]);
    close(socks[1]);
    free(iface.name);
}

struct msghdr_testing {
    int called;
    unsigned sendlen;
};
static void process_msghdr(struct msghdr *msg, struct Packet *p, void *userdata)
{
    struct msghdr_testing *d = (struct msghdr_testing *)userdata;
    OK_FATAL(msg != NULL, "have msg");
    OK_FATAL(p != NULL, "received packet");
    OK(p->len == d->sendlen, "len %u %u", p->len, d->sendlen);
    d->called += 1;
    // we have nothing interesting in msg...
}

static void test_common_recv(void)
{
    int socks[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, socks) < 0) {
        SKIP("can't create sockets: %s", strerror(errno));
    }

    // a minimal interface
    struct Interface iface;
    memset(&iface, 0, sizeof(iface));
    iface.name = strdup("dummy for testing");
    iface.state = IFSTATE_OPEN;
    iface.recvfd = socks[0];

    unsigned char sendbuf[1024];
    for (unsigned i=0; i<sizeof(sendbuf); i++) {
        sendbuf[i] = i % 256;
    }

    for (unsigned sendlen=1; sendlen<500; sendlen+=50) {
        if (send(socks[1], sendbuf, sendlen, 0) != sendlen) {
            SKIP("can't send: %s", strerror(errno));
        }

        struct Packet *p = iface_common_recv(&iface, NULL, NULL);
        OK_FATAL(p != NULL, "received packet");
        OK_FATAL(p->buf != NULL, "have packet buffer");
        OK(p->start == PACKET_START_OFFSET, "start %u", p->start);
        OK(p->len == sendlen, "length %u %u", sendlen, p->len);
        for (unsigned j=0; j<sendlen; j++) {
            OK(sendbuf[j] == p->buf[p->start+j], "byte %u differs %u %u", j,
                    sendbuf[j], p->buf[p->start+j]);
        }
        OK(p->scratch_len == 0, "scratch_len %u", p->scratch_len);
        OK(p->from == &iface, "from");
        OK(p->header_count == 0, "header_count %u", p->header_count);
        //TODO can we have recv_time on our socketpair?

        OK(delete_packet(p) == NULL, "always returns null");
    }

    // test the msghdr callback
    unsigned sendlen = 30;
    if (send(socks[1], sendbuf, sendlen, 0) != sendlen) {
        SKIP("can't send: %s", strerror(errno));
    }
    struct msghdr_testing d = { 0, sendlen };
    struct Packet *p = iface_common_recv(&iface, process_msghdr, &d);
    OK_FATAL(p != NULL, "received packet");
    OK_FATAL(p->buf != NULL, "have packet buffer");
    OK(p->len == sendlen, "length %u %u", sendlen, p->len);
    OK(d.called == 1, "called %u", d.called);
    OK(delete_packet(p) == NULL, "always returns null");

    // test receiving lots of packets
    struct Packet *pp[PACKET_COUNT_LIMIT*2];
    for (unsigned i=0; i<PACKET_COUNT_LIMIT*2; i++) {
        if (send(socks[1], sendbuf, sendlen, 0) != sendlen) {
            SKIP("can't send: %s", strerror(errno));
        }
        pp[i] = iface_common_recv(&iface, NULL, NULL);
        if (i < PACKET_COUNT_LIMIT) {
            OK_FATAL(pp[i] != NULL, "received packet %u", i);
            OK_FATAL(pp[i]->buf != NULL, "have packet buffer");
            OK(packet_dummy(pp[i]) == false, "dummy %u", i);
            OK(pp[i]->len == sendlen, "length %u %u", sendlen, pp[i]->len);
        } else {
            // we shouldn't get dummy buffers
            OK_FATAL(pp[i] == NULL, "received packet %u", i);
        }
    }
    for (unsigned i=0; i<PACKET_COUNT_LIMIT*2; i++) {
        OK(delete_packet(pp[i]) == NULL, "always returns null");
    }

    close(socks[0]);
    close(socks[1]);
    free(iface.name);
}

TEST_CASES = {
    {"common send", test_common_send},
    {"common recv", test_common_recv},
    {NULL, NULL}
};

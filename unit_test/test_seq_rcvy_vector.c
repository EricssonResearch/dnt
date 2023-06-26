
#include "testing.h"

#include "seq_recov.h"
#include "packet.h"
#include "utils.h"

#include <stdlib.h>

#include <unistd.h>
#include <arpa/inet.h> /* htonl() */

TEST_INIT("Sequence Recovery: Vector");

static const unsigned history_length = 64; // must be 2^n
static const unsigned reset_ms = 30;

static void test_window(void)
{
    struct SequenceRecovery *rec = new_seq_rec(RCVY_Vector, false, false, history_length, reset_ms, 2);
    OK_FATAL(rec, "have object");

    struct Packet *p = new_packet(NULL);
    OK_FATAL(p, "have packet");

    unsigned start = 200;

    p->sequence = htonl(start);
    OK(seq_recovery(rec, p) == true, "in TakeAny");
    p->sequence = htonl(start+1);
    OK(seq_recovery(rec, p) == true, "not duplicate and move window");
    p->sequence = htonl(start-1);
    OK(seq_recovery(rec, p) == true, "not duplicate");
    p->sequence = htonl(start);
    OK(seq_recovery(rec, p) == false, "duplicate");
    p->sequence = htonl(start+1);
    OK(seq_recovery(rec, p) == false, "duplicate");
    p->sequence = htonl(start-1);
    OK(seq_recovery(rec, p) == false, "duplicate");

    // window center is at start+1 now

    p->sequence = htonl(start+1 - history_length);
    OK(seq_recovery(rec, p) == false, "window edge outside");
    p->sequence = htonl(start+1 - history_length+1);
    OK(seq_recovery(rec, p) == true, "window edge inside");

    p->sequence = htonl(start+1 + history_length);
    OK(seq_recovery(rec, p) == false, "window edge outside");
    p->sequence = htonl(start+1 + history_length-1);
    OK(seq_recovery(rec, p) == true, "window edge inside and move window");

    // window center is at start+history_length now

    p->sequence = htonl(start+history_length - history_length);
    OK(seq_recovery(rec, p) == false, "window edge outside");
    p->sequence = htonl(start+history_length - history_length+1); // we already had start+1
    OK(seq_recovery(rec, p) == false, "duplicate");
    p->sequence = htonl(start+history_length - history_length+2);
    OK(seq_recovery(rec, p) == true, "window edge inside");

    // move window center to start+history_length*1.5
    p->sequence = htonl(start+history_length + history_length/2);
    OK(seq_recovery(rec, p) == true, "not duplicate and move window");
    p->sequence = htonl(start+history_length*1.5 - history_length);
    OK(seq_recovery(rec, p) == false, "window edge outside");
    p->sequence = htonl(start+history_length*1.5 - history_length+1);
    OK(seq_recovery(rec, p) == true, "window edge inside");

    unsigned newstart = 2000;
    p->sequence = htonl(newstart);
    OK(seq_recovery(rec, p) == false, "outside");
    usleep(1000*(reset_ms+30)); //TODO the needed oversleep depends on cpu speed :(
    OK(seq_recovery(rec, p) == true, "in TakeAny again");

    // sequence wrap-around
    usleep(1000*(reset_ms+30));
    newstart = 65535;
    p->sequence = htonl(newstart);
    OK(seq_recovery(rec, p) == true, "in TakeAny again");
    p->sequence = htonl(20);
    OK(seq_recovery(rec, p) == true, "in window");
    p->sequence = htonl(65520);
    OK(seq_recovery(rec, p) == true, "in window");

    OK(delete_seq_rec(rec) == NULL, "delete object");
    OK(delete_packet(p) == NULL, "delete packet");
}

static void test_single(void)
{
    struct SequenceRecovery *rec = new_seq_rec(RCVY_Vector, false, false, history_length, reset_ms, 2);
    OK_FATAL(rec, "have object");

    struct Packet *p = new_packet(NULL);
    OK_FATAL(p, "have packet");

    srand(2020); // this seed looks nice

    const unsigned start = history_length * 3;
    const unsigned length = history_length * 10;
    int results[start + length + history_length*2];
    for (unsigned i=0; i<ARRAY_SIZE(results); i++)
        results[i] = 0;

    int indices[history_length*4];
    for (unsigned i=0; i<ARRAY_SIZE(indices); i++) {
        indices[i] = i - history_length*2;
    }

    for (unsigned j=0; j<length; j++) {
        // scramble the indices
        for (unsigned i=0; i<ARRAY_SIZE(indices); i++) {
            unsigned r = rand() % ARRAY_SIZE(indices);
            int d = indices[i];
            indices[i] = indices[r];
            indices[r] = d;
        }
        /*for (unsigned i=0; i<ARRAY_SIZE(indices); i++) {
          printf(" %d", indices[i]);
        }
        printf("\n");*/

        for (unsigned i=0; i<ARRAY_SIZE(indices); i++) {
            unsigned d = start + j + indices[i];
            p->sequence = htonl(d);
            results[d] += seq_recovery(rec, p);
        }
    }
    for (unsigned i=0; i<ARRAY_SIZE(results); i++)
        OK(results[i] < 2, "duplicate %u", i);

    OK(delete_seq_rec(rec) == NULL, "delete object");
    OK(delete_packet(p) == NULL, "delete packet");
}

static void test_multi(void)
{
    SKIP("first implement the locking");

    //TODO same as test_single but from two threads simultaneously
    //TODO they should start at the same time (main thread releases a semaphore)
    //TODO testing.h cannot handle multithreading, we must not do OK() in the threads

}

TEST_CASES = {
    {"Window", test_window},
    {"Stress Single-Thread", test_single},
    {"Stress Multi-Thread", test_multi},
    {NULL, NULL}
};

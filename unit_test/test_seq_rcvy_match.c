
#include "testing.h"

#include "seq_recov.h"
#include "packet.h"
#include "utils.h"

#include <stdlib.h>

#include <unistd.h>

TEST_INIT("Sequence Recovery: Match");

static const unsigned history_length = 64; // must be 2^n
static const unsigned reset_ms = 30;

static void test_duplicates(void)
{
    // note: only @reset_ms has effect
    struct SequenceRecovery *rec = new_seq_rec(RCVY_Match, false, false, history_length, reset_ms, 2, NULL);
    OK_FATAL(rec, "have object");

    unsigned start = 200;
    unsigned sequence;

    sequence = start;
    OK(seq_recovery(rec, sequence) == true, "in TakeAny");
    OK(seq_recovery(rec, sequence) == false, "duplicate");
    sequence = start+1;
    OK(seq_recovery(rec, sequence) == true, "not duplicate");
    OK(seq_recovery(rec, sequence) == false, "duplicate");
    sequence = start;
    OK(seq_recovery(rec, sequence) == true, "not duplicate");
    OK(seq_recovery(rec, sequence) == false, "duplicate");
    sequence = start-1;
    OK(seq_recovery(rec, sequence) == true, "not duplicate");
    OK(seq_recovery(rec, sequence) == false, "duplicate");
    sequence = start;
    OK(seq_recovery(rec, sequence) == true, "not duplicate");
    OK(seq_recovery(rec, sequence) == false, "duplicate");

    usleep(1000*(reset_ms+30)); //TODO the needed oversleep depends on cpu speed :(
    OK(seq_recovery(rec, sequence) == true, "in TakeAny again");
    OK(seq_recovery(rec, sequence) == false, "duplicate");

    // test the seq overflow point
    sequence = 0xffff;
    OK(seq_recovery(rec, sequence) == true, "not duplicate");
    OK(seq_recovery(rec, sequence) == false, "duplicate");
    sequence = 0;
    OK(seq_recovery(rec, sequence) == true, "not duplicate");
    OK(seq_recovery(rec, sequence) == false, "duplicate");
    sequence = 0xffff;
    OK(seq_recovery(rec, sequence) == true, "not duplicate");
    OK(seq_recovery(rec, sequence) == false, "duplicate");

    OK(delete_seq_rec(rec) == NULL, "delete object");
}

static void test_single(void)
{
    struct SequenceRecovery *rec = new_seq_rec(RCVY_Match, false, false, history_length, reset_ms, 2, NULL);
    OK_FATAL(rec, "have object");

    srand(2020); // this seed looks nice

    const unsigned start = 2023;
    const unsigned interval = 100;
    const unsigned iterations = 10000;
    unsigned last = start-1;
    for (unsigned i=0; i<iterations; i++) {
        unsigned seq = start + rand() % interval;
        unsigned sequence = seq;
        bool result = seq_recovery(rec, sequence);
        bool good = last != seq;
        OK(result == good, "match %u last %u seq %u result %d", i, last, seq, result);
        last = seq;
    }

    OK(delete_seq_rec(rec) == NULL, "delete object");
}

static void test_multi(void)
{
    SKIP("first implement the locking");

    // note: match recovery is intended for slow streams
}

TEST_CASES = {
    {"duplicates", test_duplicates},
    {"Stress Single-Thread", test_single},
    {"Stress Multi-Thread", test_multi},
    {NULL, NULL}
};

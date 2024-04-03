
#include "testing.h"

#include "time_utils.h"

#include <arpa/inet.h>

TEST_INIT("Time Utils");

//TODO test the timespec operations? do we trust the BSD folks?

static void test_msec(void)
{
    struct timespec ts;
    unsigned msec;

    msec = 1;
    timespec_from_msec(&ts, msec);
    OK(ts.tv_sec == 0 && ts.tv_nsec == 1000000, "sec %ld nsec %ld", ts.tv_sec, ts.tv_nsec);
    msec = 1000;
    timespec_from_msec(&ts, msec);
    OK(ts.tv_sec == 1 && ts.tv_nsec == 0, "sec %ld nsec %ld", ts.tv_sec, ts.tv_nsec);
    msec = 1111;
    timespec_from_msec(&ts, msec);
    OK(ts.tv_sec == 1 && ts.tv_nsec == 111000000, "sec %ld nsec %ld", ts.tv_sec, ts.tv_nsec);
    msec = 11111;
    timespec_from_msec(&ts, msec);
    OK(ts.tv_sec == 11 && ts.tv_nsec == 111000000, "sec %ld nsec %ld", ts.tv_sec, ts.tv_nsec);

    ts = (struct timespec){.tv_sec = 0, .tv_nsec = 1};
    timespec_to_msec(msec, &ts);
    OK(msec == 0, "msec %u", msec);
    ts = (struct timespec){.tv_sec = 1, .tv_nsec = 1};
    timespec_to_msec(msec, &ts);
    OK(msec == 1000, "msec %u", msec);
    ts = (struct timespec){.tv_sec = 0, .tv_nsec = 1000};
    timespec_to_msec(msec, &ts);
    OK(msec == 0, "msec %u", msec);
    ts = (struct timespec){.tv_sec = 0, .tv_nsec = 999999};
    timespec_to_msec(msec, &ts);
    OK(msec == 0, "msec %u", msec);
    ts = (struct timespec){.tv_sec = 1, .tv_nsec = 1000000};
    timespec_to_msec(msec, &ts);
    OK(msec == 1001, "msec %u", msec);
    ts = (struct timespec){.tv_sec = 0, .tv_nsec = 999999999};
    timespec_to_msec(msec, &ts);
    OK(msec == 999, "msec %u", msec);
    ts = (struct timespec){.tv_sec = 9, .tv_nsec = 999999999};
    timespec_to_msec(msec, &ts);
    OK(msec == 9999, "msec %u", msec);
}

static void test_tsn(void)
{
    struct timespec ts;
    struct timespec now;
    unsigned tsn;

    ts = (struct timespec){.tv_sec = 0, .tv_nsec = 0};
    timespec_to_tsntstamp(tsn, &ts);
    OK(ntohl(tsn) == 0x08000000u, "tsn %u 0x%.8x", ntohl(tsn), ntohl(tsn));
    ts = (struct timespec){.tv_sec = 0, .tv_nsec = 1000};
    timespec_to_tsntstamp(tsn, &ts);
    OK(ntohl(tsn) == 0x08000001u, "tsn %u 0x%.8x", ntohl(tsn), ntohl(tsn));
    ts = (struct timespec){.tv_sec = 0, .tv_nsec = 10000};
    timespec_to_tsntstamp(tsn, &ts);
    OK(ntohl(tsn) == 0x0800000au, "tsn %u 0x%.8x", ntohl(tsn), ntohl(tsn));
    ts = (struct timespec){.tv_sec = 0, .tv_nsec = 100000};
    timespec_to_tsntstamp(tsn, &ts);
    OK(ntohl(tsn) == 0x08000064u, "tsn %u 0x%.8x", ntohl(tsn), ntohl(tsn));
    ts = (struct timespec){.tv_sec = 0, .tv_nsec = 1000000};
    timespec_to_tsntstamp(tsn, &ts);
    OK(ntohl(tsn) == 0x080003e8u, "tsn %u 0x%.8x", ntohl(tsn), ntohl(tsn));
    ts = (struct timespec){.tv_sec = 0, .tv_nsec = 10000000};
    timespec_to_tsntstamp(tsn, &ts);
    OK(ntohl(tsn) == 0x08002710u, "tsn %u 0x%.8x", ntohl(tsn), ntohl(tsn));
    ts = (struct timespec){.tv_sec = 0, .tv_nsec = 100000000};
    timespec_to_tsntstamp(tsn, &ts);
    OK(ntohl(tsn) == 0x080186a0u, "tsn %u 0x%.8x", ntohl(tsn), ntohl(tsn));
    ts = (struct timespec){.tv_sec = 0, .tv_nsec = 999999000};
    timespec_to_tsntstamp(tsn, &ts);
    OK(ntohl(tsn) == 0x080f423fu, "tsn %u 0x%.8x", ntohl(tsn), ntohl(tsn));
    ts = (struct timespec){.tv_sec = 1, .tv_nsec = 0};
    timespec_to_tsntstamp(tsn, &ts);
    OK(ntohl(tsn) == 0x08100000u, "tsn %u 0x%.8x", ntohl(tsn), ntohl(tsn));
    ts = (struct timespec){.tv_sec = 10, .tv_nsec = 0};
    timespec_to_tsntstamp(tsn, &ts);
    OK(ntohl(tsn) == 0x08000000u, "tsn %u 0x%.8x", ntohl(tsn), ntohl(tsn));
    ts = (struct timespec){.tv_sec = 11, .tv_nsec = 0};
    timespec_to_tsntstamp(tsn, &ts);
    OK(ntohl(tsn) == 0x08100000u, "tsn %u 0x%.8x", ntohl(tsn), ntohl(tsn));
    ts = (struct timespec){.tv_sec = 10, .tv_nsec = 1000000};
    timespec_to_tsntstamp(tsn, &ts);
    OK(ntohl(tsn) == 0x080003e8u, "tsn %u 0x%.8x", ntohl(tsn), ntohl(tsn));
    ts = (struct timespec){.tv_sec = 11, .tv_nsec = 1000000};
    timespec_to_tsntstamp(tsn, &ts);
    OK(ntohl(tsn) == 0x081003e8u, "tsn %u 0x%.8x", ntohl(tsn), ntohl(tsn));

    // start at an odd second
    ts  = (struct timespec){.tv_sec = 11, .tv_nsec = 400000000};
    timespec_to_tsntstamp(tsn, &ts);
    now = (struct timespec){.tv_sec = 11, .tv_nsec = 400000000}; // same time
    timespec_from_tsntstamp(&ts, tsn, &now);
    OK(ts.tv_sec == 11 && ts.tv_nsec == 400000000, "sec %ld nsec %ld", ts.tv_sec, ts.tv_nsec);
    now = (struct timespec){.tv_sec = 11, .tv_nsec = 400000001}; // little later
    timespec_from_tsntstamp(&ts, tsn, &now);
    OK(ts.tv_sec == 11 && ts.tv_nsec == 400000000, "sec %ld nsec %ld", ts.tv_sec, ts.tv_nsec);
    now = (struct timespec){.tv_sec = 11, .tv_nsec = 400001000}; // one us later
    timespec_from_tsntstamp(&ts, tsn, &now);
    OK(ts.tv_sec == 11 && ts.tv_nsec == 400000000, "sec %ld nsec %ld", ts.tv_sec, ts.tv_nsec);
    now = (struct timespec){.tv_sec = 11, .tv_nsec = 500000000}; // little more later
    timespec_from_tsntstamp(&ts, tsn, &now);
    OK(ts.tv_sec == 11 && ts.tv_nsec == 400000000, "sec %ld nsec %ld", ts.tv_sec, ts.tv_nsec);
    now = (struct timespec){.tv_sec = 12, .tv_nsec = 399999000}; // almost one second later
    timespec_from_tsntstamp(&ts, tsn, &now);
    OK(ts.tv_sec == 11 && ts.tv_nsec == 400000000, "sec %ld nsec %ld", ts.tv_sec, ts.tv_nsec);
    now = (struct timespec){.tv_sec = 12, .tv_nsec = 400000000}; // exactly one second later
    timespec_from_tsntstamp(&ts, tsn, &now);
    OK(ts.tv_sec == 11 && ts.tv_nsec == 400000000, "sec %ld nsec %ld", ts.tv_sec, ts.tv_nsec);
    now = (struct timespec){.tv_sec = 12, .tv_nsec = 400001000}; // one second one us later
    timespec_from_tsntstamp(&ts, tsn, &now);
    OK(ts.tv_sec == 11 && ts.tv_nsec == 400000000, "sec %ld nsec %ld", ts.tv_sec, ts.tv_nsec);
    now = (struct timespec){.tv_sec = 12, .tv_nsec = 500000000}; // more than one second later
    timespec_from_tsntstamp(&ts, tsn, &now);
    OK(ts.tv_sec == 11 && ts.tv_nsec == 400000000, "sec %ld nsec %ld", ts.tv_sec, ts.tv_nsec);
    now = (struct timespec){.tv_sec = 13, .tv_nsec = 300000000}; // less than two seconds later
    timespec_from_tsntstamp(&ts, tsn, &now);
    OK(ts.tv_sec == 11 && ts.tv_nsec == 400000000, "sec %ld nsec %ld", ts.tv_sec, ts.tv_nsec);
    now = (struct timespec){.tv_sec = 13, .tv_nsec = 399999000}; // almost two seconds later
    timespec_from_tsntstamp(&ts, tsn, &now);
    OK(ts.tv_sec == 11 && ts.tv_nsec == 400000000, "sec %ld nsec %ld", ts.tv_sec, ts.tv_nsec);
    now = (struct timespec){.tv_sec = 13, .tv_nsec = 400000000}; // exactly two seconds later
    timespec_from_tsntstamp(&ts, tsn, &now);
    OK(ts.tv_sec == 13 && ts.tv_nsec == 400000000, "sec %ld nsec %ld", ts.tv_sec, ts.tv_nsec); // this is over the limit
    now = (struct timespec){.tv_sec = 13, .tv_nsec = 400001000}; // two seconds one us later
    timespec_from_tsntstamp(&ts, tsn, &now);
    OK(ts.tv_sec == 13 && ts.tv_nsec == 400000000, "sec %ld nsec %ld", ts.tv_sec, ts.tv_nsec); // this is over the limit
    now = (struct timespec){.tv_sec = 11, .tv_nsec = 399999000}; // earlier than ts
    timespec_from_tsntstamp(&ts, tsn, &now);
    OK(ts.tv_sec ==  9 && ts.tv_nsec == 400000000, "sec %ld nsec %ld", ts.tv_sec, ts.tv_nsec); // too early

    // same thing with even second
    ts  = (struct timespec){.tv_sec = 12, .tv_nsec = 400000000};
    timespec_to_tsntstamp(tsn, &ts);
    now = (struct timespec){.tv_sec = 12, .tv_nsec = 400000000}; // same time
    timespec_from_tsntstamp(&ts, tsn, &now);
    OK(ts.tv_sec == 12 && ts.tv_nsec == 400000000, "sec %ld nsec %ld", ts.tv_sec, ts.tv_nsec);
    now = (struct timespec){.tv_sec = 12, .tv_nsec = 400000001}; // little later
    timespec_from_tsntstamp(&ts, tsn, &now);
    OK(ts.tv_sec == 12 && ts.tv_nsec == 400000000, "sec %ld nsec %ld", ts.tv_sec, ts.tv_nsec);
    now = (struct timespec){.tv_sec = 12, .tv_nsec = 400001000}; // one us later
    timespec_from_tsntstamp(&ts, tsn, &now);
    OK(ts.tv_sec == 12 && ts.tv_nsec == 400000000, "sec %ld nsec %ld", ts.tv_sec, ts.tv_nsec);
    now = (struct timespec){.tv_sec = 12, .tv_nsec = 500000000}; // little more later
    timespec_from_tsntstamp(&ts, tsn, &now);
    OK(ts.tv_sec == 12 && ts.tv_nsec == 400000000, "sec %ld nsec %ld", ts.tv_sec, ts.tv_nsec);
    now = (struct timespec){.tv_sec = 13, .tv_nsec = 399999000}; // almost one second later
    timespec_from_tsntstamp(&ts, tsn, &now);
    OK(ts.tv_sec == 12 && ts.tv_nsec == 400000000, "sec %ld nsec %ld", ts.tv_sec, ts.tv_nsec);
    now = (struct timespec){.tv_sec = 13, .tv_nsec = 400000000}; // exactly one second later
    timespec_from_tsntstamp(&ts, tsn, &now);
    OK(ts.tv_sec == 12 && ts.tv_nsec == 400000000, "sec %ld nsec %ld", ts.tv_sec, ts.tv_nsec);
    now = (struct timespec){.tv_sec = 13, .tv_nsec = 400001000}; // one second one us later
    timespec_from_tsntstamp(&ts, tsn, &now);
    OK(ts.tv_sec == 12 && ts.tv_nsec == 400000000, "sec %ld nsec %ld", ts.tv_sec, ts.tv_nsec);
    now = (struct timespec){.tv_sec = 13, .tv_nsec = 500000000}; // more than one second later
    timespec_from_tsntstamp(&ts, tsn, &now);
    OK(ts.tv_sec == 12 && ts.tv_nsec == 400000000, "sec %ld nsec %ld", ts.tv_sec, ts.tv_nsec);
    now = (struct timespec){.tv_sec = 14, .tv_nsec = 300000000}; // less than two seconds later
    timespec_from_tsntstamp(&ts, tsn, &now);
    OK(ts.tv_sec == 12 && ts.tv_nsec == 400000000, "sec %ld nsec %ld", ts.tv_sec, ts.tv_nsec);
    now = (struct timespec){.tv_sec = 14, .tv_nsec = 399999000}; // almost two seconds later
    timespec_from_tsntstamp(&ts, tsn, &now);
    OK(ts.tv_sec == 12 && ts.tv_nsec == 400000000, "sec %ld nsec %ld", ts.tv_sec, ts.tv_nsec);
    now = (struct timespec){.tv_sec = 14, .tv_nsec = 400000000}; // exactly two seconds later
    timespec_from_tsntstamp(&ts, tsn, &now);
    OK(ts.tv_sec == 14 && ts.tv_nsec == 400000000, "sec %ld nsec %ld", ts.tv_sec, ts.tv_nsec); // this is over the limit
    now = (struct timespec){.tv_sec = 14, .tv_nsec = 400001000}; // two seconds one us later
    timespec_from_tsntstamp(&ts, tsn, &now);
    OK(ts.tv_sec == 14 && ts.tv_nsec == 400000000, "sec %ld nsec %ld", ts.tv_sec, ts.tv_nsec); // this is over the limit
    now = (struct timespec){.tv_sec = 12, .tv_nsec = 399999000}; // earlier than ts
    timespec_from_tsntstamp(&ts, tsn, &now);
    OK(ts.tv_sec == 10 && ts.tv_nsec == 400000000, "sec %ld nsec %ld", ts.tv_sec, ts.tv_nsec); // too early
}

TEST_CASES = {
    {"msec", test_msec},
    {"tsn", test_tsn},
    {NULL, NULL}
};

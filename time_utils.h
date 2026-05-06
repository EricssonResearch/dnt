// Copyright (c) 2023-2025, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_TIME_UTILS_H
#define R2_TIME_UTILS_H

#include <stdint.h>
#include <time.h>

#include <arpa/inet.h> /* htonl, ntohl */

/* Operations on timespecs */
/* stolen from https://github.com/freebsd/freebsd-src/blob/main/sys/sys/time.h */
/* License: BSD-3-Clause */
/* TODO why doesn't GNU libc have these?? */
#define	timespecclear(tvp)	((tvp)->tv_sec = (tvp)->tv_nsec = 0)
#define	timespecisset(tvp)	((tvp)->tv_sec || (tvp)->tv_nsec)
#define	timespeccmp(tvp, uvp, cmp)					\
	(((tvp)->tv_sec == (uvp)->tv_sec) ?				\
	    ((tvp)->tv_nsec cmp (uvp)->tv_nsec) :			\
	    ((tvp)->tv_sec cmp (uvp)->tv_sec))

#define	timespecadd(tsp, usp, vsp)					\
	do {								\
		(vsp)->tv_sec = (tsp)->tv_sec + (usp)->tv_sec;		\
		(vsp)->tv_nsec = (tsp)->tv_nsec + (usp)->tv_nsec;	\
		if ((vsp)->tv_nsec >= 1000000000L) {			\
			(vsp)->tv_sec++;				\
			(vsp)->tv_nsec -= 1000000000L;			\
		}							\
	} while (0)
#define	timespecsub(tsp, usp, vsp)					\
	do {								\
		(vsp)->tv_sec = (tsp)->tv_sec - (usp)->tv_sec;		\
		(vsp)->tv_nsec = (tsp)->tv_nsec - (usp)->tv_nsec;	\
		if ((vsp)->tv_nsec < 0) {				\
			(vsp)->tv_sec--;				\
			(vsp)->tv_nsec += 1000000000L;			\
		}							\
	} while (0)
#define	timespecvalid_interval(tsp)	((tsp)->tv_sec >= 0 &&		\
	    (tsp)->tv_nsec >= 0 && (tsp)->tv_nsec < 1000000000L)
/* end of timespec operations */

#define NSEC_PER_SEC 1000000000L

#define timespec_from_msec(tsp, msec)                       \
    do {                                                    \
        (tsp)->tv_sec = (msec) / 1000;                      \
        (tsp)->tv_nsec = ((msec) % 1000) * 1000000;         \
    } while (0)

#define timespec_to_msec(msec, tsp)                         \
    do {                                                    \
        (msec) = (tsp)->tv_nsec / 1000000                   \
               + (tsp)->tv_sec * 1000;                      \
    } while (0)

#define timespec_from_usec(tsp, usec)                       \
    do {                                                    \
        (tsp)->tv_sec = (usec) / 1000000;	                \
	    (tsp)->tv_nsec = ((usec) % 1000000) * 1000;         \
    } while (0)

#define timespec_to_usec(usec, tsp)                         \
    do {                                                    \
        (usec) = (tsp)->tv_nsec / 1000                      \
               + (tsp)->tv_sec * 1000000;                   \
    } while (0)

// TSN timestamp format:
//      20 bit microsec (enough to count to 999999)
//      21th bit is sec (in total we can count up to 2 seconds)
//      0x08000000 is the TTAG indicator bit
#define timespec_to_tsntstamp(tsnts, tsp)                   \
    do {                                                    \
        (tsnts) = 0x08000000u + (((tsp)->tv_sec % 2) << 20) \
                + (((tsp)->tv_nsec / 1000) & 0xfffffu);     \
        (tsnts) = htonl(tsnts);                             \
    } while (0)

// this assumes that @tsnts was less than 2 seconds before @now
#define timespec_from_tsntstamp(tsp, tsnts, now)            \
    do {                                                    \
        unsigned _ts = ntohl(tsnts);                        \
        (tsp)->tv_sec = (_ts >> 20) & 1;                    \
        (tsp)->tv_nsec = (_ts & 0xfffffu) * 1000;           \
        if (((now)->tv_sec & 1) == (tsp)->tv_sec) {         \
            if ((now)->tv_nsec >= (tsp)->tv_nsec) {         \
                (tsp)->tv_sec = (now)->tv_sec;              \
            } else {                                        \
                (tsp)->tv_sec = (now)->tv_sec - 2;          \
            }                                               \
        } else {                                            \
            (tsp)->tv_sec = (now)->tv_sec - 1;              \
        }                                                   \
    } while (0)

//TODO convert the macros above into inline functions

// returns t1-t2
static inline int64_t time_diff_us(struct timespec t1, struct timespec t2)
{
    int64_t timediff;
    timediff = (t1.tv_nsec - t2.tv_nsec) / 1000;
    timediff += (t1.tv_sec - t2.tv_sec) * 1000000;
    return timediff;
}

static inline struct timespec time_add_us(struct timespec t, unsigned increase_us)
{
    t.tv_sec += increase_us / 1000000;
    t.tv_nsec += (increase_us % 1000000) * 1000;
    t.tv_sec += t.tv_nsec / 1000000000;
    t.tv_nsec %= 1000000000;
    return t;
}


#endif // R2_TIME_UTILS_H

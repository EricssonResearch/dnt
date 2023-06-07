// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_TIME_UTILS_H
#define R2_TIME_UTILS_H

#include <time.h>

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

//#define NSEC_PER_SEC 1000000000L - already defined in utils.h

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

#define timespec_to_tsntstamp(tsnts, tsp)                   \
    do {                                                    \
        (tsnts) = 0x08000000 + (((tsp)->tv_sec % 2) << 20)  \
                + (((tsp)->tv_nsec / 1000) & 0xfffff);      \
        (tsnts) = htonl(tsnts);                             \
    } while (0)

// this assumes that @tsnts was less than 2 seconds before @now
#define timespec_from_tsntstamp(tsp, tsnts, now)            \
    do {                                                    \
        unsigned ts = ntohl(tsnts);                         \
        (tsp)->tv_sec = (ts >> 20) & 1;                     \
        (tsp)->tv_nsec = (ts & 0xfffff) * 1000;             \
        if (((now)->tv_sec & 1) == (tsp)->tv_sec) {         \
            if ((now)->tv_nsec > (tsp)->tv_nsec) {          \
                (tsp)->tv_sec = (now)->tv_sec;              \
            } else {                                        \
                (tsp)->tv_sec = (now)->tv_sec - 2;          \
            }                                               \
        } else {                                            \
            (tsp)->tv_sec = (now)->tv_sec - 1;              \
        }                                                   \
    } while (0)


#endif // R2_TIME_UTILS_H

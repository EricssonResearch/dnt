
#define _GNU_SOURCE

#include "delay.h"
#include "utils.h"
#include "action.h"
#include "pipeline.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/select.h>


//TODO we need a thread_utils.h

#define DEBUG_TSTAMP

#ifndef timespeccmp
#define	timespeccmp(_ts, _us, cmp)					\
	(((_ts).tv_sec == (_us).tv_sec) ?			  	\
	    ((_ts).tv_nsec cmp (_us).tv_nsec) :			\
	    ((_ts).tv_sec cmp (_us).tv_sec))
#endif

static inline struct timespec timespec_diff(struct timespec a, struct timespec b) {
    struct timespec result;
    result.tv_sec  = a.tv_sec  - b.tv_sec;
    result.tv_nsec = a.tv_nsec - b.tv_nsec;
    while (result.tv_nsec < 0) {
        --result.tv_sec;
        result.tv_nsec += 1000000000L;
    }
    return result;
}

static void print_tstamp(void)
{
#ifdef DEBUG_TSTAMP
        struct timespec time_now;
        clock_gettime(CLOCK_REALTIME, &time_now);
        printf("* %lu.%09ld ", time_now.tv_sec,time_now.tv_nsec);  // timestamp debug
#endif
}

struct DelayQueue {
    //TODO keep a list of PipelineIterator objects (priority queue)
    //      a background thread will get them from the queue and pipe_iterator_run()
    struct PipelineIterator *pi;
    unsigned delay;
    struct timespec due_time;
    struct DelayQueue *next;
};

static struct DelayQueue *delay_queue = NULL;
static pthread_t delay_tid;
static sem_t delay_sem;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
struct timespec delay_timer;
int ev_fds;

static void *delay_thread(void *arg)
{
    (void)arg;
    printf("Delay thread starting\n");
    pthread_setname_np(pthread_self(), "delay thread");

    fd_set  readfds;
    int nfds = ev_fds+1;
    struct timespec time_now, wait;

    int ret, val;

    FD_ZERO(&readfds);

    // wait for the first packet to be delayed
    sem_wait(&delay_sem);


    while (1) {
      FD_SET(ev_fds, &readfds);
      clock_gettime(CLOCK_REALTIME, &time_now);
      wait = timespec_diff(delay_timer, time_now);
      ret = pselect(nfds, &readfds, 0, 0, &wait, NULL);
      if(ret > 0){
          ret = read(ev_fds, &val, sizeof(val));
          //printf("event interrupted, %d\n", val);
          continue; // start over to recalculate timer
        }

      pthread_mutex_lock (&mutex);

      // delay time elapsed, send out
      struct DelayQueue* pDelayQueueFirst = delay_queue;
      if(pDelayQueueFirst == NULL){
          printf("ERROR: Packet delay timer expired but queue empty.\n");
          pthread_mutex_unlock (&mutex);
          sem_wait(&delay_sem);
          continue;
      }

      struct PipelineIterator *pi = pDelayQueueFirst->pi;
      //struct Action *a = pi->pipe->actions[pi->pos];

      print_tstamp();
      printf(" *****************************************  delayed send \n");



      //TODO when we get a PipelineIterator from the queue the current action is the delay
      //      we need to step to the next action before pipe_iterator_run()
      pi->pos++;
      pipe_iterator_run(pi);

      // Move  to the next frame in tt queue
      delay_queue = pDelayQueueFirst->next;

      pthread_mutex_unlock (&mutex);

      // Free frame
      //free_packet(tt_queue_first->data);           // free data

      free(pDelayQueueFirst);

      // Check if the tt queue is empty
      if(delay_queue != NULL){               // more packets in queue, prime timer
        delay_timer.tv_sec = delay_queue->due_time.tv_sec;
        delay_timer.tv_nsec = delay_queue->due_time.tv_nsec;

#if defined (DEBUG) || defined (DEBUG_TSTAMP)
        printf(" timer %ld.%09ld", delay_queue->due_time.tv_sec, delay_queue->due_time.tv_nsec);
#endif
      // it will sleep

      }else{
          // no follow-up packet, just wait for semaphore
          sem_wait(&delay_sem);
      }

    }

    return NULL;
}

bool init_delay(void)
{
    if (sem_init(&delay_sem, 0, 0) < 0) {
        perror("init_delay sem_init");
        return false;
    }

    //delay_queue = calloc_struct(DelayQueue);

    pthread_attr_t attr;
    if ((errno = pthread_attr_init(&attr)) != 0) {
        perror("delay thread pthread_attr_init");
        return false;
    }
    if ((errno = pthread_attr_setschedpolicy(&attr, SCHED_FIFO)) != 0) {
        perror("delay thread pthread_attr_setschedpolicy");
        return false;
    }
    struct sched_param param;
    param.sched_priority = 97;
    if ((errno = pthread_attr_setschedparam(&attr, &param)) != 0) {
        perror("delay thread pthread_attr_setschedparam");
        return false;
    }
    if ((errno = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED)) != 0) {
        perror("pthread_attr_setinheritsched");
        return -1;
    }

    if (pthread_create(&delay_tid, &attr, &delay_thread, NULL) != 0) {
        fprintf(stderr, "could not create delay thread\n");
        return false;
    }

    ev_fds = eventfd(0, EFD_NONBLOCK);
    if(ev_fds == -1){
        fprintf(stderr, "Create eventfd failed. \n");
        return false;
    }

    return true;
}

void fini_delay(void)
{
    pthread_cancel(delay_tid); //TODO flush the queue first
    pthread_join(delay_tid, NULL);

    free(delay_queue);
}

void delay_insert(struct PipelineIterator *pi, unsigned timestamp, unsigned delay)
{
  // alloc and fill in the tt_queue entry
  struct DelayQueue* pDelayQueueEntry = calloc_struct(DelayQueue);
  if(pDelayQueueEntry == NULL){
    printf("Insufficient memory.\n");
    // TODO handle error
    return;
  }

  pDelayQueueEntry->pi = pi;
  pDelayQueueEntry->delay = delay;
  pDelayQueueEntry->next = NULL;

  // get current time
  clock_gettime(CLOCK_REALTIME, &pDelayQueueEntry->due_time);

  unsigned ts_now = (pDelayQueueEntry->due_time.tv_nsec/1000) | (pDelayQueueEntry->due_time.tv_sec & 0x00000001)<<20;

#ifdef DEBUG_TSTAMP
  struct timespec t2 = pDelayQueueEntry->due_time;
  printf(" now %u ts %u diff=%d", ts_now, timestamp, (ts_now-timestamp>1000000)?ts_now-timestamp-1000000:ts_now-timestamp);  // timestamp debug
#endif

  if(ts_now<timestamp){
      // the first bit switched from 1 to 0
      pDelayQueueEntry->due_time.tv_sec -= 1;
      pDelayQueueEntry->due_time.tv_nsec = (timestamp & 0x0FFFFF)*1000;
  } else {
      // replace lower bits from tstamp
      pDelayQueueEntry->due_time.tv_sec = (pDelayQueueEntry->due_time.tv_sec & 0xFFFFFFFE) | ((timestamp >> 20) & 0x00000001);
      pDelayQueueEntry->due_time.tv_nsec = (timestamp & 0x0FFFFF)*1000;
    }
  // Add delay configured in ms to the timestamp received (we delay tt_delay_ms from tstamp received)
  // The configured ms delay value should not be > 1s
  pDelayQueueEntry->due_time.tv_nsec += delay*1000000;
  if(pDelayQueueEntry->due_time.tv_nsec >= 1000000000) {
      pDelayQueueEntry->due_time.tv_sec++;
      pDelayQueueEntry->due_time.tv_nsec -= 1000000000;
  }

  // handling the delay queue should not be interrupted
  pthread_mutex_lock (&mutex);

  // find the frame in the tt_queue where we have to insert
  struct DelayQueue *pDelayQueueIterator, *pDelayQueueIteratorPrev;

  pDelayQueueIterator = (struct DelayQueue *) delay_queue; pDelayQueueIteratorPrev = NULL;
  while(pDelayQueueIterator != NULL){
    //printf("\n\t\t* %lu:%lu > %lu:%lu",pttq_iter->due_time.tv_sec, pttq_iter->due_time.tv_nsec,pttqe->due_time.tv_sec, pttqe->due_time.tv_nsec);
    if(timespeccmp(pDelayQueueIterator->due_time, pDelayQueueEntry->due_time, >))
        break;
    pDelayQueueIteratorPrev = pDelayQueueIterator;
    pDelayQueueIterator = pDelayQueueIterator->next;
  }


  int val = 1;
  if(pDelayQueueIteratorPrev == NULL){

    // if the  queue not empty, set the next
    // if(tt_queue != NULL)
    pDelayQueueEntry->next = (struct DelayQueue *) delay_queue;

    // first in the delay queue, set timer for it
    delay_timer.tv_sec = pDelayQueueEntry->due_time.tv_sec;
    delay_timer.tv_nsec = pDelayQueueEntry->due_time.tv_nsec;

    // first in the queue?
    if(delay_queue == NULL){
      delay_queue = pDelayQueueEntry;

      // unlock mutex
      pthread_mutex_unlock (&mutex);

      sem_post(&delay_sem);
    }else{
      // this should be first, interrupt waiting...
      delay_queue = pDelayQueueEntry;

      // interrupt current pselect
      if(write(ev_fds, &val, sizeof(val)) <= 0) printf ("write event fd error!!!");       // TODO: THROW

      // unlock mutex
      pthread_mutex_unlock (&mutex);
    }
  } else {  // not first, insert into the queue
    if(delay_queue == NULL) printf("Should not happen\n");

    // put in the queue
    pDelayQueueEntry->next = pDelayQueueIteratorPrev->next;
    pDelayQueueIteratorPrev->next = pDelayQueueEntry;
    // timer must be running, not needed to set...
    pthread_mutex_unlock (&mutex);
  }

  #if defined (DEBUG_TSTAMP)
    if(pDelayQueueEntry->due_time.tv_nsec - t2.tv_nsec < 0)
      printf(" delay: %ld.%09ld\n", pDelayQueueEntry->due_time.tv_sec-1-t2.tv_sec, pDelayQueueEntry->due_time.tv_nsec -t2.tv_nsec + 1000000000);
    else
      printf(" delay: %ld.%09ld\n", pDelayQueueEntry->due_time.tv_sec-t2.tv_sec, pDelayQueueEntry->due_time.tv_nsec -t2.tv_nsec);
  #endif

}

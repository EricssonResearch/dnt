// Copyright (c) 2023, Ericsson AB and Ericsson Telecommunication Hungary
// All rights reserved.


#ifndef R2_THREAD_H
#define R2_THREAD_H

// handle for a named background thread
struct Thread;

// create a named thread, @thread_fn receives @thread_arg
struct Thread *thread_launch(void* (*thread_fn)(void *), void *thread_arg, const char *name, ...)
    __attribute__((format(printf, 3, 4)))
    __attribute__((nonnull(3)))
    __attribute__((nonnull(1)));

// create a named thread, @thread_fn receives @thread_arg
// fails unless we have CAP_SYS_NICE
struct Thread *thread_launch_priority(void* (*thread_fn)(void *), void *thread_arg, int priority, const char *name, ...)
    __attribute__((format(printf, 4, 5)))
    __attribute__((nonnull(4)))
    __attribute__((nonnull(1)));

// stop the given thread and free the resources
// doesn't do anything if called from the thread itself
// always returns NULL
// TODO return error somehow if the thread already exited
struct Thread *thread_stop(struct Thread *thread);

// the thread stops itself
// the other threads don't need to wait for it or join with it
// doesn't do anything if called from another thread
void thread_exit(struct Thread *thread);

// send alarm signal for a thread
void thread_wakeup(const struct Thread *thread);

// returns the name of the thread
const char *thread_getname(const struct Thread *thread);

// returns an unique identifier number for the thread
unsigned thread_getid(const struct Thread *thread);


// thread-safe FIFO queue
// intended for one-way message passing between threads
struct MessageQueue;

// creates an empty queue
struct MessageQueue *new_messagequeue(void);

// deletes the queue
// messages still in the queue will be lost
// always returns NULL
struct MessageQueue *delete_messagequeue(struct MessageQueue *q);

// adds @message to the end of the queue
void messagequeue_push(struct MessageQueue *q, void *message);

// wait while @q is empty or @usec microseconds have elapsed
// returns immediately if there is an item in @q
// removes and returns the first item
// returns NULL on timeout
// if there are multiple items in @q, the one pushed first is returned
// negative @usec means no timeout (waits indefinitely)
// zero @usec means return immediately even if @q is empty
void *messagequeue_pop(struct MessageQueue *q, int usec);

// calls @cb for each item in the queue, @userdata is passed to @cb
// stops and returns false when @cb returns false
// returns true on success
int messagequeue_foreach(struct MessageQueue *q, int (*cb)(const void *item, void *userdata), void *userdata);


#endif // R2_THREAD_H

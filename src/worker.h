/*
 * The Loomiere Project (http://valeriu.palos.ro/loomiere/).
 *
 * worker.h: Streaming worker thread.
 *
 * Read the LICENSE file!
 * Copyright (C)2010 Valeriu Paloş (valeriu@palos.ro). All rights reserved!
 */

#ifndef __worker_h__
#define __worker_h__

/*----------------------------------------------------------------------------------------------------------*/

#include <ev.h>
#include <lua.h>
#include <lauxlib.h>
#include <pthread.h>
#include <stddef.h>
#include <tcadb.h>

#include "core.h"
#include "stream.h"

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Commands types coming through the incoming queue.
 */
enum {
    COMMAND_NONE,
    COMMAND_STOP,
    COMMAND_LOAD,
    COMMAND_ZERO
};

/*
 * Command node for the incoming queue.
 */
typedef struct task_node_t {

    // internals
    int                 command;
    stream_t*           stream;
    struct task_node_t* next;
    struct task_node_t* prev;

    // alignment
    CACHE_ALIGNMENT(    sizeof(int) +
                        sizeof(stream_t*) +
                        sizeof(struct task_node_t*) * 2);
} task_node_t CACHE_ALIGNED;

/*
 * Worker object.
 */
typedef struct worker_t {

    // arguments
    size_t              id;             // worker id code
    TCADB*              db;             // cache database

    // internals
    size_t              load;           // active streams
    size_t              data_total;     // data sent since pivot time
    ev_tstamp           data_pivot;     // start-time of transfer measurement
    size_t              cache_hits;     // number of successful db gets
    size_t              cache_misses;   // number of failed db gets
    double              delay_sum;      // total sum of delays
    double              delay_count;    // total number of delays
    double              delay_average;  // total number of delays

    pthread_t           thread;         // thread handle
    pthread_spinlock_t  lock;           // spinlock
    task_node_t*        head;           // incoming queue head
    task_node_t*        tail;           // incoming queue tail

    struct ev_loop*     loop;           // event loop
    lua_State*          lua;            // utility Lua state

    ev_async            async_w;        // asynchronous command handler

    // alignment
    CACHE_ALIGNMENT(    sizeof(size_t) * 5 +
                        sizeof(double) * 3 +
                        sizeof(ev_tstamp) +
                        sizeof(pthread_t) +
                        sizeof(pthread_spinlock_t) +
                        sizeof(task_node_t*) * 2 +
                        sizeof(TCADB*) +
                        sizeof(struct ev_loop*) +
                        sizeof(lua_State*) +
                        sizeof(ev_async));

} worker_t CACHE_ALIGNED;

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Constructor (arguments are prepared in self).
 */
int worker_new(worker_t* self);

/*
 * Destructor.
 */
int worker_destroy(worker_t* self);

/*
 * Enqueue an uninitialized (but defined) stream (assumes self is valid).
 * Returns 0 on success and 1 otherwise. On success the stream is fully
 * taken over by this worker for the rest of its life.
 */
int worker_enqueue(worker_t* self, stream_t* stream);

/*
 * Reset statistics.
 */
int worker_zero(worker_t* self);

/*----------------------------------------------------------------------------------------------------------*/

#endif

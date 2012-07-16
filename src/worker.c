/*
 * The Loomiere Project (http://valeriu.palos.ro/loomiere/).
 *
 * worker.c: Streaming worker thread.
 *
 * Read the LICENSE file!
 * Copyright (C)2010 Valeriu Paloş (valeriu@palos.ro). All rights reserved!
 */

#include <pthread.h>

#include "amf.h"
#include "worker.h"

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Send an asynchronous command to the worker.
 * Returns 0 on success and 1 on error (queue is full).
 */
static int _queue_push(worker_t* self, int command, stream_t* stream) {

    // prepare node
    task_node_t* node = (task_node_t*)ZALLOC(sizeof(task_node_t));
    node->command = command;
    node->stream = stream;

    // acquire lock
    pthread_spin_lock(&self->lock);

    // append
    node->prev = self->tail->prev;
    node->next = self->tail;
    self->tail->prev->next = node;
    self->tail->prev = node;

    // release lock
    pthread_spin_unlock(&self->lock);

    // wake-up thread
    ev_async_send(self->loop, &self->async_w);

    // done
    return 0;
}

/*
 * Retrieves a pending command from the incoming queue.
 * The command code and stream object (if available) are put in the
 * 'command' and 'stream' arguments reslectively. The function will
 * return 0 on success and 1 if queue was empty.
 */
static int _queue_pop(worker_t* self, int* command, stream_t** stream) {

    // prepare
    task_node_t* node = NULL;
    if (command) {
        *command = COMMAND_NONE;
    }
    if (stream) {
        *stream = NULL;
    }

    // acquire lock
    pthread_spin_lock(&self->lock);

    // pop
    int empty = 1;
    if (self->head->next != self->tail) {

        // extract
        node = self->head->next;
        self->head->next = node->next;
        node->next->prev = self->head;

        // assign
        if (command) *command = node->command;
        if (stream) *stream = node->stream;

        // ready
        FREE(node);
        empty = 0;
    }

    // release lock
    pthread_spin_unlock(&self->lock);

    // done
    return empty;
}

/*
 * Asynchronous command processor.
 */
static void _worker_async_cb(EV_P_ ev_async* watcher, int events) {

    // get self
    worker_t* self    = (worker_t*)((char*)watcher - offsetof(worker_t, async_w));
    int       command = COMMAND_NONE;
    stream_t* stream  = NULL;

    // consume
    while (!_queue_pop(self, &command, &stream)) {

        // handle
        switch (command) {

        // shutdown
        case COMMAND_STOP:
            ev_unloop(self->loop, EVUNLOOP_ALL);
            break;

        // enqueue
        case COMMAND_LOAD:
            if (stream_new(stream)) {
                stream_destroy(stream);
                FREE(stream);
            }
            break;

        // reset statistics
        case COMMAND_ZERO:
            self->delay_sum = 0;
            self->delay_count = 0;
            self->delay_average = 0;
            break;

        // ignore
        default:
            break;
        }
    }
}

/*
 * Worker main work loop.
 */
static void* _worker_run(void* data) {

    // get self
    worker_t* self = (worker_t*)data;

    // configure
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

    // capture commands
    ev_async_init(&self->async_w, _worker_async_cb);
    ev_async_start(self->loop, &self->async_w);

    // enter loop
    TRACE("Worker %u is up.", self->id);
    ev_loop(self->loop, 0);

    // end gracefully
    TRACE("Worker %u is down!", self->id);
    pthread_exit(NULL);
}

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Constructor.
 */
int worker_new(worker_t* self) {

    // initialise
    pthread_spin_init(&self->lock, 0);
    self->head = (task_node_t*)ZALLOC(sizeof(task_node_t));
    self->tail = (task_node_t*)ZALLOC(sizeof(task_node_t));
    self->head->next = self->tail;
    self->tail->prev = self->head;

    // event loop
    self->loop = ev_loop_new(0);
    if (!self->loop) {
        ERROR("Could not create new event loop for worker %u!", self->id);
    }

    // statistics
    self->data_pivot = ev_now(self->loop);

    // Lua state
    self->lua = lua_open();
    luaL_openlibs(self->lua);
    load_core_c(self->lua);
    load_amf_lua(self->lua);

    // parsers
    stream_flv_setup(self);
    stream_mp4_setup(self);

    // spawn
    return pthread_create(&self->thread, NULL, _worker_run, self);
}

/*
 * Destructor.
 */
int worker_destroy(worker_t* self) {

    // send stop command
    _queue_push(self, COMMAND_STOP, NULL);

    // cancel if failed
    if (pthread_join(self->thread, NULL)) {
        pthread_cancel(self->thread);
        WARNING("Worker %u stalled, and was cancelled!", self->id);
    }

    // purge internals
    ev_loop_destroy(self->loop);
    lua_close(self->lua);
    pthread_spin_destroy(&self->lock);
    FREE(self->head);
    FREE(self->tail);

    // done
    ZERO(self, sizeof(worker_t));
    return 0;
}

/*
 * Enqueue an uninitialized (but defined) stream (assumes self is valid).
 * Returns 0 on success and 1 otherwise. On success the stream is fully
 * taken over by this worker for the rest of its life.
 */
int worker_enqueue(worker_t* self, stream_t* stream) {

    // pass-on context
    stream->db = self->db;
    stream->loop = self->loop;
    stream->lua = self->lua;

    // pass-on statistics
    stream->load = &self->load;
    stream->cache_hits = &self->cache_hits;
    stream->cache_misses = &self->cache_misses;
    stream->data_total = &self->data_total;
    stream->delay_sum = &self->delay_sum;
    stream->delay_count = &self->delay_count;
    stream->delay_average = &self->delay_average;

    // enlist stream
    return _queue_push(self, COMMAND_LOAD, stream);
}


/*
 * Reset statistics.
 */
int worker_zero(worker_t* self) {
    _queue_push(self, COMMAND_ZERO, NULL);
}

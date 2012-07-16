/*
 * The Loomiere Project (http://valeriu.palos.ro/loomiere/).
 *
 * engine.h: Multi-threaded, event-based, streaming engine.
 *
 * Read the LICENSE file!
 * Copyright (C)2010 Valeriu Paloş (valeriu@palos.ro). All rights reserved!
 */

#ifndef __engine_h__
#define __engine_h__

/*----------------------------------------------------------------------------------------------------------*/

#include <lua.h>
#include <lauxlib.h>
#include <pthread.h>
#include <tcadb.h>

#include "core.h"
#include "stream.h"
#include "worker.h"

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Engine indicators values.
 */
enum {
    ENGINE_LOAD,
    ENGINE_CACHE_USED,
    ENGINE_CACHE_ITEMS,
    ENGINE_CACHE_HITS,
    ENGINE_CACHE_MISSES,
    ENGINE_DATA_TOTAL,
    ENGINE_DATA_DELAY
};

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Engine object.
 */
typedef struct engine_t {

    // arguments
    unsigned int        workers;
    unsigned int        clients;
    double              throttle;
    unsigned long       cache;

    // internals
    worker_t*           pool;
    pthread_spinlock_t  lock;
    TCADB*              db;

    // alignment
    CACHE_ALIGNMENT(    sizeof(unsigned int) * 2 +
                        sizeof(unsigned long) +
                        sizeof(double) +
                        sizeof(worker_t*) +
                        sizeof(pthread_spinlock_t) +
                        sizeof(TCADB*));

} engine_t CACHE_ALIGNED;


/*----------------------------------------------------------------------------------------------------------*/

/*
 * Constructor (arguments are prepared in self).
 */
int engine_new(engine_t* self);

/*
 * Destructor.
 */
int engine_destroy(engine_t* self);

/*
 * Get an engine indicator value.
 */
double engine_monitor(engine_t* self, int indicator);

/*
 * Dispatch a stream to a worker (assumes self and stream are valid).
 * Returns 0 on success, 1 on error.
 */
int engine_dispatch(engine_t* self, stream_t* stream);

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Extract engine object from Lua table found at given index.
 */
engine_t* extract_engine(lua_State* L, int index);

/*
 * Expose engine to Lua.
 */
int load_engine_c(lua_State* L);

/*----------------------------------------------------------------------------------------------------------*/

#endif

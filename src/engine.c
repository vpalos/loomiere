/*
 * The Loomiere Project (http://valeriu.palos.ro/loomiere/).
 *
 * engine.c: Multi-threaded, event-based, streaming engine.
 *
 * Read the LICENSE file!
 * Copyright (C)2010 Valeriu Paloş (valeriu@palos.ro). All rights reserved!
 */

#include <ev.h>
#include <math.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#include "engine.h"

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Constructor.
 */
int engine_new(engine_t* self) {

    // initialize
    self->pool = (worker_t*)ZALLOC(sizeof(worker_t) * self->workers);
    pthread_spin_init(&self->lock, 0);

    // cache
    if (self->cache) {
        char* options = FORMAT("*#capsiz=%lu", self->cache);
        self->db = tcadbnew();
        if (!tcadbopen(self->db, options)) {
            tcadbdel(self->db);
            self->db = NULL;
        }
        FREE(options);
    } else {
        self->db = NULL;
    }

    // workers
    int i;
    for (i = 0; i < self->workers; i++) {
        self->pool[i].id = i + 1;
        self->pool[i].db = self->db;
        if (worker_new(&self->pool[i])) {
            FATAL("Failed to create worker %u!", i + 1);
        }
    }

    // await workers (.25s)
    usleep(250000);

    // success
    return 0;
}

/*
 * Destructor.
 */
int engine_destroy(engine_t* self) {

    // workers
    int i = self->workers - 1;
    for (; i >= 0 ; i--) {
        worker_destroy(&self->pool[i]);
    }

    // cache
    if (self->db) {
        tcadbclose(self->db);
        tcadbdel(self->db);
    }

    // deinitialize
    pthread_spin_destroy(&self->lock);
    FREE(self->pool);

    // done
    ZERO(self, sizeof(engine_t));
    return 0;
}

/*
 * Get number of active clients.
 */
 double engine_monitor(engine_t* self, int indicator) {

    // initial value
    double result = 0;
    int i;

    // handle
    switch (indicator) {

    // active clients
    case ENGINE_LOAD:
        for (i = 0; i < self->workers; i++) {
            result += (double)self->pool[i].load;
        }
        break;

    // cache indicators
    case ENGINE_CACHE_USED:
        if (self->db) {
            result = tcadbsize(self->db);
        }
        break;
    case ENGINE_CACHE_ITEMS:
        if (self->db) {
            result = tcadbrnum(self->db);
        }
        break;
    case ENGINE_CACHE_HITS:
        for (i = 0; i < self->workers; i++) {
            result += (double)self->pool[i].cache_hits;
        }
        break;
    case ENGINE_CACHE_MISSES:
        for (i = 0; i < self->workers; i++) {
            result += (double)self->pool[i].cache_misses;
        }
        break;

    // transfer indicators
    case ENGINE_DATA_TOTAL:
        for (i = 0; i < self->workers; i++) {
            ev_tstamp now = ev_now(self->pool[i].loop);
            ev_tstamp delta = now - self->pool[i].data_pivot;
            if (self->pool[i].data_pivot) {
                result += (double)self->pool[i].data_total / (delta ? delta : 1);
                if (delta > 1. && self->pool[i].data_total) {
                    self->pool[i].data_total = 0;
                    self->pool[i].data_pivot = now;
                }
            }
        }
        break;
    case ENGINE_DATA_DELAY:
        for (i = 0; i < self->workers; i++) {
            result += self->pool[i].delay_average;
            worker_zero(&self->pool[i]);
        }
        result = result / (double)self->workers;
        break;

    // unknown
    default:
        break;
    }

    // ready
    return result;
}

/*
 * Dispatch a stream to a worker (assumes self and stream are valid).
 * Returns 0 on success, 1 on error.
 */
int engine_dispatch(engine_t* self, stream_t* stream) {

    // choose
    int i = 1;
    int minimum = self->pool[0].load;
    worker_t* worker = &self->pool[0];
    for (; i < self->workers; i++) {
        if (minimum > self->pool[i].load) {
            minimum = self->pool[i].load;
            worker = &self->pool[i];
        }
    }

    // configure
    stream->throttle = self->throttle;

    // ready
    return worker_enqueue(worker, stream);
}

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Lua: utility routines.
 */
static engine_t* luaU_engine_self(lua_State* L, int index, int pop) {

    // check self
    if (!lua_istable(L, index)) goto error;

    // extract
    lua_getfield(L, index, "__ud");
    engine_t* self = (engine_t*)lua_touserdata(L, -1);
    lua_pop(L, pop != 0);

    // check __ud
    if (!self) goto error;

    // success
    return self;

    // error
    error:
    luaL_error(L, "Expected a valid 'engine' instance!");
    return NULL;
}

static int luaF_engine_gc(lua_State* L) {

    // get engine
    engine_t* self = (engine_t*)lua_touserdata(L, -1);

    // unmeta
    lua_pushnil(L);
    lua_setmetatable(L, -2);

    // handle
    engine_destroy(self);

    // ready
    return 0;
}

/*
 * Lua: object method wrappers.
 */

// [0, +1, -]
// (self, {}) => userdata
static int luaF_engine_new(lua_State* L) {

    // instance
    if (!lua_istable(L, 1)) {
        luaL_error(L, "Invalid 'self' given to engine:new()");
    } else if (lua_gettop(L) < 2) {
        lua_newtable(L);
    }

    // metatable
    lua_pushvalue(L, 1);
    lua_setfield(L, 1, "__index");      // self.__index = self
    lua_pushcfunction(L, luaF_engine_gc);
    lua_setfield(L, 1, "__gc");         // self.__gc = engine.gc
    lua_pushvalue(L, 1);
    lua_setmetatable(L, 2);             // setmetatable(instance, self)

    // userdata
    engine_t* engine = (engine_t*)lua_newuserdata(L, sizeof(engine_t));
    ZERO(engine, sizeof(engine_t));
    lua_pushvalue(L, 1);
    lua_setmetatable(L, -2);            // setmetatable(<userdata>, self)
    lua_setfield(L, 2, "__ud");         // instance.__ud = <userdata>

    // options
    lua_getfield(L, 2, "workers");
    lua_getfield(L, 2, "clients");
    lua_getfield(L, 2, "throttle");
    lua_getfield(L, 2, "cache");
    engine->workers = (unsigned int)luaL_checkinteger(L, -4);
    engine->clients = (unsigned int)luaL_checkinteger(L, -3);
    engine->throttle = (double)luaL_checknumber(L, -2);
    engine->cache = (double)luaL_checknumber(L, -1);
    lua_pop(L, 4);

    // attempt ignition
    if (engine_new(engine)) {
        lua_pop(L, 1);
        lua_pushnil(L);
    }

    // ready
    return 1;
}

// [0, 0, -]
// (self) => -
static int luaF_engine_destroy(lua_State* L) {

    // check self
    luaU_engine_self(L, 1, 0);

    // call gc
    luaF_engine_gc(L);

    // ready
    lua_pop(L, 1);
    return 0;
}

// [0, +1, -]
// (self, {}) => boolean
static int luaF_engine_dispatch(lua_State* L) {

    // get engine
    engine_t* self = extract_engine(L, 1);

    // stream definition
    if (!lua_istable(L, 2)) goto error_invalid;

    // create stream
    stream_t* stream = (stream_t*)ZALLOC(sizeof(stream_t));

    // check overload
    if (self->clients &&
        engine_monitor(self, ENGINE_LOAD) >= self->clients)
        goto error_overload;

    // extract client (socket:getfd(), request.http)
    lua_getfield(L, 2, "client");                                           // push client
    lua_getfield(L, -1, "socket");                                          // push socket
    lua_getfield(L, -1, "getfd");                                           // push getfd
    lua_pushvalue(L, -2);                                                   // push self (socket)
    if (lua_pcall(L, 1, 1, 0)) goto error_invalid;                          // call client.socket:getfd()
    stream->socket = luaL_checkinteger(L, -1);                              // read socket descriptor
    lua_pop(L, 1);                                                          // pop socket descriptor
    lua_getfield(L, -1, "setfd");                                           // push setfd
    lua_pushvalue(L, -2);                                                   // push self (socket)
    lua_pushinteger(L, -1);                                                 // push -1 (dummy descriptor)
    if (lua_pcall(L, 2, 0, 0)) goto error_invalid;                          // call client.socket:setfd(-1)
    lua_pop(L, 1);                                                          // pop socket
    lua_getfield(L, -1, "request");                                         // push request
    lua_getfield(L, -1, "http");                                            // push http
    strncpy(stream->http, luaL_checkstring(L, -1), sizeof(stream->http));   // read http
    lua_pop(L, 3);                                                          // pop http, request, client

    // The period is currently always 1 but this
    // allows the period to be made configurable
    // in the future if needed, via options.lua!
    stream->period = 1.0;

    // get arguments
    lua_getfield(L, 2, "path");
    lua_getfield(L, 2, "mime");
    lua_getfield(L, 2, "spatial");
    lua_getfield(L, 2, "start");
    lua_getfield(L, 2, "stop");
    stream->path = STRDUP(luaL_checkstring(L, -5));
    stream->mime = STRDUP(luaL_checkstring(L, -4));
    stream->spatial = lua_toboolean(L, -3);
    stream->start = luaL_checknumber(L, -2);
    stream->stop = luaL_checknumber(L, -1);
    lua_pop(L, 5);

    // dispatch
    if (engine_dispatch(self, stream)) goto error_overload;

    // success
    lua_pushboolean(L, 1);
    return 1;

    // overload
    error_overload:
    FREE(stream);
    lua_pushnil(L);
    lua_pushstring(L, "overload");
    return 2;

    // invalid definition
    error_invalid:
    FREE(stream);
    lua_pushnil(L);
    lua_pushstring(L, "invalid");
    return 2;
}


// [0, +1, -]
// (self, indicator) => number
static int luaF_engine_monitor(lua_State* L) {

    // get engine
    engine_t* self = extract_engine(L, 1);

    // nominal indicator table
    static const char* const indicator_names[] = {
        "load",
        "cache:used",
        "cache:items",
        "cache:hits",
        "cache:misses",
        "data:total",
        "data:delay",
        NULL
    };

    // scalar indicator table
    static const int indicator_values[] = {
        ENGINE_LOAD,
        ENGINE_CACHE_USED,
        ENGINE_CACHE_ITEMS,
        ENGINE_CACHE_HITS,
        ENGINE_CACHE_MISSES,
        ENGINE_DATA_TOTAL,
        ENGINE_DATA_DELAY,
        0
    };

    // get indicator
    int indicator = luaL_checkoption(L, 2, NULL, indicator_names);

    // get value
    double result = engine_monitor(self, indicator_values[indicator]);

    // done
    lua_pushnumber(L, result);
    return 1;
}

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Extract engine object from Lua table found at given index.
 */
engine_t* extract_engine(lua_State* L, int index) {
    return luaU_engine_self(L, index, 1);
}

/*
 * Expose engine to Lua.
 */
int load_engine_c(lua_State* L) {

    // assemble
    static const luaL_Reg engine_lib[] = {
        { "new", luaF_engine_new },
        { "destroy", luaF_engine_destroy },
        { "dispatch", luaF_engine_dispatch },
        { "monitor", luaF_engine_monitor },
        { NULL, NULL }
    };

    // register
    luaL_register(L, "engine", engine_lib);

    // defaults
    lua_pushinteger(L, 2);
    lua_setfield(L, -2, "workers");
    lua_pushinteger(L, 1000);
    lua_setfield(L, -2, "clients");
    lua_pushnumber(L, 20.0);
    lua_setfield(L, -2, "throttle");
    lua_pushinteger(L, 256 * 1048576);
    lua_setfield(L, -2, "cache");

    // finish
    return 1;
}

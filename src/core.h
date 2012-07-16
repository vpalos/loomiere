/*
 * The Loomiere Project (http://valeriu.palos.ro/loomiere/).
 *
 * core.h: Essential routines and structures.
 *
 * Read the LICENSE file!
 * Copyright (C)2010 Valeriu Paloş (valeriu@palos.ro). All rights reserved!
 */

#ifndef __core_h__
#define __core_h__

/*----------------------------------------------------------------------------------------------------------*/

#include <lua.h>
#include <lauxlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Cache line alignment, essential for maximum paralell performance.
 */
#ifndef CACHE_LINE_SIZE
    #warning "Cache-line size is unknown, assuming 64 bytes!"
    #define CACHE_LINE_SIZE 64
#endif

#define CACHE_ALIGNED           __attribute__ ((aligned(CACHE_LINE_SIZE)))
#define CACHE_ALIGNMENT(size)   char __cache_line_alignment__[CACHE_LINE_SIZE - (size) % CACHE_LINE_SIZE]

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Logging system.
 *
 * Minimalistic logging mechanism for dumping messages at stderr which
 * should be captured and rotated using a service logger (e.g. svlogd).
 * Debug messages (traces) are dumped only when running in DEBUG mode.
 */
void LOG(const char* level, const char* format, ...);

#define INFO(format, ...)       { LOG("INFO", format, ##__VA_ARGS__); }
#define WARNING(format, ...)    { LOG("WARNING", format, ##__VA_ARGS__); }
#define ERROR(format, ...)      { LOG("ERROR", format, ##__VA_ARGS__); }
#define FATAL(format, ...)      { LOG("ERROR", format, ##__VA_ARGS__); exit(-1); }

#ifdef DEBUG
    #define TRACE(format, ...)  { LOG("TRACE", "[#%x] " format, (unsigned)pthread_self(), ##__VA_ARGS__); }
#else
    #define TRACE(format, ...)  { }
#endif

#define HINT(format, ...)       { LOG("HINT", format, ##__VA_ARGS__); }

/* -------------------------------------------------------------------------------------------------------- */

/*
 * Precisely allocate and format a string (without ever failing).
 * The returned pointer must eventually be released using FREE().
 */
char* FORMAT(const char* format, ...);

/* -------------------------------------------------------------------------------------------------------- */

/*
 * Memory function-wrappers that never fail.
 */
void*   ALLOC(size_t size);
void*   REALLOC(void* pointer, size_t size);
void*   ZALLOC(size_t size);
void    ZERO(void* pointer, size_t size);
char*   STRDUP(const char* original);
char*   STRNDUP(const char* original, size_t length);
#define FREE(p) { free(p); p = NULL; }

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Math utility macros.
 */
#define MAX(a, b)       ((a) > (b) ? (a) : (b))
#define MIN(a, b)       ((a) < (b) ? (a) : (b))
#define ROUND(a)        ((uint64_t)((a) + 0.5))

/*
 * Big-endian I/O macros.
 */
#define read_24(b)      read_xx(b, 24)
#define read_32(b)      read_xx(b, 32)
#define read_64(b)      read_xx(b, 64)
#define write_32(b, v)  write_xx(b, v, 32)
#define write_64(b, v)  write_xx(b, v, 64)

/*
 * Binary access routines.
 */
uint64_t read_xx(const uint8_t*, uint8_t);
void write_xx(uint8_t*, uint64_t, uint8_t);

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Expose core to Lua.
 */
int load_core_c(lua_State* L);

/*----------------------------------------------------------------------------------------------------------*/

#endif

/*
 * The Loomiere Project (http://valeriu.palos.ro/loomiere/).
 *
 * core.c: Essential routines and structures.
 *
 * Read the LICENSE file!
 * Copyright (C)2010 Valeriu Paloş (valeriu@palos.ro). All rights reserved!
 */

#define _GNU_SOURCE
#define _XOPEN_SOURCE 600

#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "core.h"

/* -------------------------------------------------------------------------------------------------------- */

/*
 * Logging system.
 *
 * Minimalistic logging mechanism for dumping messages at stderr which
 * should be captured and rotated using a service logger (e.g. svlogd).
 * Debug messages (traces) are dumped only when running in DEBUG mode.
 */
void LOG(const char* level, const char* format, ...) {
    time_t r = time(NULL);
    struct tm t;
    localtime_r(&r, &t);
    static __thread char message[1024];

    va_list va;
    va_start(va, format);
    vsnprintf(message, 1023, format, va);
    va_end(va);

    if (strcmp(level, "HINT")) {
        fprintf(stderr, "%04d-%02d-%02d %02d:%02d:%02d %s - %s\n",
                        t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                        t.tm_hour, t.tm_min, t.tm_sec, level, message);
        fflush(stderr);
    } else {
        printf("(?) %s\n", message);
        fflush(stdout);
    }
    
}

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Precisely allocate and format a string (without ever failing).
 * The returned pointer must eventually be released using FREE().
 */
char* FORMAT(const char* format, ...) {

    // prepare
    int     result   = 0;
    int     length   = 256;
    char*   data     = (char*)ALLOC(length);
    va_list args;

    // (re)try
    while (1) {

        // attempt
        va_start(args, format);
        result = vsnprintf(data, length, format, args);
        va_end(args);

        // check
        if (result > -1 && result < length) {
            break;
        }

        // calibrate
        if (result > -1) {
            length = result + 1;
        } else {;
            length *= 2;
        }

        // reallocate
        data = REALLOC(data, length);
    }

    // ready
    return data;
}

/*
 * Pread wrapper that return 0 only if the read was perfect, or 1 otherwise.
 */
int PREAD(int file, void* buffer, size_t bytes, off_t offset) {
    return pread(file, buffer, bytes, offset) != bytes;
}

/* -------------------------------------------------------------------------------------------------------- */

/*
 * Memory function-wrappers that never fail.
 */
void* ALLOC(size_t size) {
    void *p = malloc(size);
    if (!p) {
        FATAL("malloc(): Not enough memory!");
    }
    return p;
}

void* REALLOC(void* pointer, size_t size) {
    void *p = realloc(pointer, size);
    if (!p) {
        FATAL("realloc(): Not enough memory!");
    }
    return p;
}

void* ZALLOC(size_t size) {
    void *p = calloc(1, size);
    if (!p) {
        FATAL("calloc(): Not enough memory!");
    }
    return p;
}

void ZERO(void* pointer, size_t size) {
    if (pointer) {
        memset(pointer, 0, size);
    }
}

char* STRDUP(const char* original) {
    void *copy = NULL;
    if (original) {
        copy = strdup(original);
        if (!copy) {
            FATAL("Not enough memory!");
        }
    }
    return copy;
}

char* STRNDUP(const char* original, size_t length) {
    void *copy = NULL;
    if (original) {
        copy = strndup(original, length);
        if (!copy) {
            FATAL("Not enough memory!");
        }
    }
    return copy;
}

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Read variable bitsize, big-endian value from buffer.
 */
uint64_t read_xx(const uint8_t* buffer, uint8_t bits) {
    uint64_t result = 0;
    uint8_t i;
    uint8_t bytes = bits >> 3;
    for (i = 0, bits -= 8; i < bytes; i++, bits -= 8) {
        result |= (uint64_t)buffer[i] << bits;
    }
    return result;
}

/*
 * Write variable bitsize, big-endian value into buffer.
 */
void write_xx(uint8_t* buffer, uint64_t value, uint8_t bits) {
    uint8_t i;
    uint8_t bytes = bits >> 3;
    for (i = 0, bits -= 8; i < bytes; i++, bits -= 8) {
        buffer[i] = value >> bits;
    }
}

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Lua: system functions.
 */

// [0, +1, -]
// (path) => true/false
static int luaF_core_readable(lua_State* L) {

    // acquire
    const char *path = luaL_checkstring(L, 1);

    // resolve
    lua_pushboolean(L, !access(path, R_OK));

    // ready
    return 1;
}

// [0, +1, -]
// (fake) => 'real'
static int luaF_core_realpath(lua_State* L) {
    char target[PATH_MAX + 1];

    // acquire
    const char *source = luaL_checkstring(L, 1);
    char* result = realpath(source, target);

    // resolve
    if (!result) {
        lua_pushnil(L);
    } else {
        lua_pushstring(L, target);
    }

    // ready
    return 1;
}

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Lua functions for handling low-level binary data.
 */

// [0, +1, -]
static int luaF_core_bin2integer32(lua_State* L) {
    // read data
    size_t length = 0;
    const unsigned char* digits = luaL_checklstring(L, 1, &length);
    if (length < 1 || length > 4) {
        lua_pushnil(L);
        return 1;
    }

    // compute
    uint32_t value = digits[0];
    int i;
    for (i = 1; i < length; i++) {
        value <<= 8;
        value += (unsigned char)digits[i];
    }

    // push back
    lua_Number result = (lua_Number)value;
    if (value == 0xffffffff) {
        result = (lua_Number)-1;
    }
    lua_pushinteger(L, result);

	// ready
	return 1;
}

// [0, +1, -]
static int luaF_core_bin2double64(lua_State* L) {

    // map value
    union {
        double value;
        char   bytes[8];
    } result;

    // read data
    size_t      length = 0;
    const char* digits = luaL_checklstring(L, 1, &length);
    if (length != 8) {
        lua_pushnil(L);
        return 1;
    }

    // detect endianness
    uint16_t endian = 1;
    int little_endian = *((char*)&endian);

    // convert
	if(little_endian) {
		result.bytes[0] = digits[7];
		result.bytes[1] = digits[6];
		result.bytes[2] = digits[5];
		result.bytes[3] = digits[4];
		result.bytes[4] = digits[3];
		result.bytes[5] = digits[2];
		result.bytes[6] = digits[1];
		result.bytes[7] = digits[0];
	} else {
		memcpy(result.bytes, digits, 8);
	}

    // push back
    lua_pushnumber(L, (lua_Number)result.value);

	// ready
	return 1;
}

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Lua: logging functions.
 */

// [0, 0, -]
static int luaU_core_log(lua_State* L, const char* level) {
    const char* message = luaL_checkstring(L, 1);
    LOG(level, "%s", message ? message : "(invalid log message)");
    return 0;
}

// [-1, 0, -]
static int luaF_core_trace(lua_State* L)   {
    #ifdef DEBUG
    return luaU_core_log(L, "TRACE");
    #else
    return 0;
    #endif
}
static int luaF_core_info(lua_State* L)    { return luaU_core_log(L, "INFO"); }
static int luaF_core_warning(lua_State* L) { return luaU_core_log(L, "WARNING"); }
static int luaF_core_error(lua_State* L)   { return luaU_core_log(L, "ERROR"); }
static int luaF_core_fatal(lua_State* L)   { luaU_core_log(L, "FATAL"); exit(-1); return 1; }
static int luaF_core_hint(lua_State* L)    { return luaU_core_log(L, "HINT"); }

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Expose core to Lua.
 */
int load_core_c(lua_State* L) {

    // signal codes
    lua_pushnumber(L, SIGINT);
    lua_setglobal(L, "SIGINT");
    lua_pushnumber(L, SIGTERM);
    lua_setglobal(L, "SIGTERM");
    lua_pushnumber(L, SIGHUP);
    lua_setglobal(L, "SIGHUP");

    // assemble
    static const luaL_Reg core_lib[] = {

        // system
        { "readable", luaF_core_readable },
        { "realpath", luaF_core_realpath },

        // binary
        { "bin2integer32", luaF_core_bin2integer32 },
        { "bin2double64", luaF_core_bin2double64 },

        // logging
        { "trace", luaF_core_trace },
        { "info", luaF_core_info },
        { "warning", luaF_core_warning },
        { "error", luaF_core_error },
        { "fatal", luaF_core_fatal },
        { "hint", luaF_core_hint },
        { NULL, NULL }
    };

    // register
    luaL_register(L, "core", core_lib);

    // finish
    return 1;
}

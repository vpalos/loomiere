/*
 * The Loomiere Project (http://valeriu.palos.ro/loomiere/).
 *
 * stream.h: A self-manageable stream object.
 *
 * Read the LICENSE file!
 * Copyright (C)2010 Valeriu Paloş (valeriu@palos.ro). All rights reserved!
 */

#ifndef __stream_h__
#define __stream_h__

/*----------------------------------------------------------------------------------------------------------*/

#include <ev.h>
#include <lua.h>
#include <lauxlib.h>
#include <stddef.h>
#include <tcadb.h>

#include "core.h"

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Streaming constants.
 */
#define STREAM_THROTTLE_FROM    1048576 // minimum length to throttle (1 MegaByte)
#define STREAM_THROTTLE_TIMEOUT 60.0    // send-timeout while playing (60 seconds)

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Stream object.
 */
typedef struct stream_t {

    // arguments
    int                 socket;         // TCP socket descriptor
    char                http[8];        // HTTP protocol version
    double              period;         // throttling period (in seconds)
    double              throttle;       // run-ahead buffer (in seconds)

    size_t*             load;           // external
    size_t*             cache_hits;     // external
    size_t*             cache_misses;   // external
    size_t*             data_total;     // external
    double*             delay_sum;      // external
    double*             delay_count;    // external
    double*             delay_average;  // external

    char*               path;           // file path on disk
    char*               mime;           // file mime-type
    int                 spatial;        // bytes if true, else seconds
    double              start;          // start position (in units)        <-- turned to seconds by parser
    double              stop;           // stop position (in units)         <-- turned to seconds by parser

    TCADB*              db;             // cache database
    struct ev_loop*     loop;           // event loop
    lua_State*          lua;            // utility Lua state

    // internals
    ev_tstamp           load_head;      // previous load-head (statistics)
    size_t              periods;        // number of offsets (periods)      <-- set by parser
    off_t*              offsets;        // file offsets for each period     <-- set by parser

    // headers i/o
    char*               head;           // headers data buffer              <-- set by parser
    off_t               head_length;    // size of headers data buffer      <-- set by parser
    off_t               head_offset;    // position within headers data     <-- set by parser

    // file-data i/o
    int                 file;           // file descriptor
    size_t              file_length;    // file size in bytes
    off_t               file_finish;    // final send target position       <-- set by parser
    off_t               file_offset;    // position within file             <-- set by parser
    off_t               file_target;    // send target position in file

    // adjustments
    int                 nagle;          // 0 = off, 1 = on
    ev_tstamp           tzero;          // timestamp of play-start

    // AMF hints i/o
    char*               hint;           // hint input buffer

    // i/o watchers
    ev_io               hint_w;         // read-hint watcher
    ev_io               send_w;         // send-file watcher
    ev_timer            wait_w;         // timeout watcher
    ev_timer            jump_w;         // future-scheduling watcher
    ev_tstamp           last_send;      // timestamp of last send

    CACHE_ALIGNMENT(    sizeof(int) * 4 +
                        sizeof(double) * 4 +
                        sizeof(double*) * 3 +
                        sizeof(off_t) * 5 +
                        sizeof(off_t*) +
                        sizeof(size_t) * 2 +
                        sizeof(size_t*) * 4+
                        sizeof(ev_io) * 2 +
                        sizeof(ev_timer) * 2 +
                        sizeof(ev_tstamp) * 3 +
                        sizeof(char) * 8 +
                        sizeof(char*) * 4 +
                        sizeof(TCADB*) +
                        sizeof(struct ev_loop*) +
                        sizeof(lua_State*));

} stream_t CACHE_ALIGNED;

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Parser function definition.
 */
typedef int (*stream_f)(stream_t*);

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Constructor (arguments must be prepared in self). If successful,
 * this function will entirely take over the created stream. In case
 * of failure (only) the caller must call stream_destroy() and FREE()
 * on the stream opbject to ensure proper clean-up.
 */
int stream_new(stream_t* self);

/*
 * Destructor.
 */
int stream_destroy(stream_t* self);

/*----------------------------------------------------------------------------------------------------------*/

#endif

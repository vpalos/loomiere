/*
 * The Loomiere Project (http://valeriu.palos.ro/loomiere/).
 *
 * stream_flv.h: FLV parser.
 *
 * Read the LICENSE file!
 * Copyright (C)2010 Valeriu Paloş (valeriu@palos.ro). All rights reserved!
 */

#ifndef __stream_flv_h__
#define __stream_flv_h__

/*----------------------------------------------------------------------------------------------------------*/

#include "core.h"
#include "stream.h"
#include "worker.h"

/*----------------------------------------------------------------------------------------------------------*/

/*
 * FLV mime.
 */
#define STREAM_FLV_MIME "video/x-flv"

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Prepare worker for FLV parsing.
 */
int stream_flv_setup(worker_t* worker);

/*
 * Parser function implementation for the FLV file format.
 */
int stream_flv_parse(stream_t* self);

/*----------------------------------------------------------------------------------------------------------*/

#endif

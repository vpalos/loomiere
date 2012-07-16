/*
 * The Loomiere Project (http://valeriu.palos.ro/loomiere/).
 *
 * stream_mp4.h: MP4 parser.
 *
 * Read the LICENSE file!
 * Copyright (C)2010 Valeriu Paloş (valeriu@palos.ro). All rights reserved!
 */

#ifndef __stream_mp4_h__
#define __stream_mp4_h__

/*----------------------------------------------------------------------------------------------------------*/

#include <stdint.h>

#include "core.h"
#include "stream.h"
#include "worker.h"

/*----------------------------------------------------------------------------------------------------------*/

/*
 * MP4 mime.
 */
#define STREAM_MP4_MIME "video/mp4"

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Atom manipulation macros.
 */
#define VOID(s)     ((s).atom.size == 0)
#define ATOM(a, b, c, d) ( \
                    ((uint32_t)(a) << 24) | \
                    ((uint32_t)(b) << 16) | \
                    ((uint32_t)(c) << 8) | \
                    ((uint32_t)(d)))

/*
 * Regular(non-optimized) atom types.
 */
#define ____        0                           // NULL atom
#define FTYP        ATOM('f', 't', 'y', 'p')    // file compatibility
#define MOOV        ATOM('m', 'o', 'o', 'v')    // movie metadata
#define CMOV        ATOM('c', 'm', 'o', 'v')    // compressed metadata
#define MVHD        ATOM('m', 'v', 'h', 'd')    // metadata headers
#define TRAK        ATOM('t', 'r', 'a', 'k')    // movie track
#define TKHD        ATOM('t', 'k', 'h', 'd')    // track header
#define MDIA        ATOM('m', 'd', 'i', 'a')    // track media
#define MDHD        ATOM('m', 'd', 'h', 'd')    // media header
#define HDLR        ATOM('h', 'd', 'l', 'r')    // media handler information
#define MINF        ATOM('m', 'i', 'n', 'f')    // media information
#define VMHD        ATOM('v', 'm', 'h', 'd')    // video media header
#define SMHD        ATOM('s', 'm', 'h', 'd')    // sound media header
#define STBL        ATOM('s', 't', 'b', 'l')    // sample tables
#define STSD        ATOM('s', 't', 's', 'd')    // sample description table
#define STTS        ATOM('s', 't', 't', 's')    // decoding time-to-sample table
#define CTTS        ATOM('c', 't', 't', 's')    // composition offsets table
#define STSS        ATOM('s', 't', 's', 's')    // sync sample table
#define STSC        ATOM('s', 't', 's', 'c')    // sample-to-chunk table
#define STSZ        ATOM('s', 't', 's', 'z')    // sample sizes table
#define STCO        ATOM('s', 't', 'c', 'o')    // 32bit chunk offsets table
#define CO64        ATOM('c', 'o', '6', '4')    // 64bit chunk offsets table
#define MDAT        ATOM('m', 'd', 'a', 't')    // media data

/*
 * Atom flags and test routines.
 */
#define F_EX        0x01                        // extended size
#define Q_EX(s)     ((s).flags & F_EX)          // has extended size?

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Gather-write buffers structure (emulation).
 */
typedef struct {
    void*  base;
    size_t size;
} iov_t;

typedef struct {
    iov_t           iovs[70];                   // assume buffer fragments
    uint32_t        count;                      // number of vectors
    uint64_t        size;
} iovs_t;

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Seek information structures.
 */
typedef struct {
    uint32_t        index;                      // entry number in table
    uint32_t        offset;                     // offset in table entry
} tbli_t;

typedef struct {
    uint64_t        time;                       // the (corrected) time to seek at (in media scale)
    uint64_t        offset;                     // place in file where sample resides

    tbli_t          stts;                       // stts table indexes
    tbli_t          ctts;                       // stts table indexes
    tbli_t          stss;                       // stts table indexes
    tbli_t          stsc;                       // stsc table indexes
    tbli_t          stsz;                       // stsz table indexes
    tbli_t          coxx;                       // 32/64bit table indexes

    uint8_t         stsc_entry[12];             // specific: stsc additional entry
} seek_t;

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Format structures.
 */
typedef struct {
    uint32_t        type;                       // type of atom as 32bit integer
    uint8_t         flags;                      // internal flags
    uint64_t        size;                       // atom size (if 0, atom is void; with header)
    uint64_t        start;                      // atom initial offset in file (with header)
    uint64_t        end;                        // atom initial end offset
    uint8_t*        data;                       // pointer to atom data (after header)
    uint64_t        data_start;                 // data offset in file (after header)
    uint64_t        data_size;                  // data size (minus header)
    uint64_t        data_position;              // position in data atom
} atom_t;

typedef struct {
    atom_t          atom;                       // atom structure
} xxxx_t;

typedef struct {
    atom_t          atom;                       // atom structure
    uint8_t         version;                    // version
    uint32_t        flags;                      // flags
    uint32_t        scale;                      // movie time-scale in units per second
    uint64_t        duration;                   // movie duration in movie time-scale units
} xxhd_t;

typedef struct {
    atom_t          atom;                       // atom structure
    uint8_t         version;                    // version
    uint32_t        flags;                      // flags
    uint32_t        size;                       // default sample size (stsz) / chunks count (stsc)
    uint32_t        count;                      // number of entries
    uint8_t*        data;                       // entries
    uint8_t         bytes;                      // table entry size in bytes
} stxx_t;

typedef struct {
    atom_t          atom;                       // atom structure

    uint64_t        max_offset;                 // byte end offset
    uint64_t        max_chunks;                 // total chunks
    uint64_t        max_samples;                // total samples
    uint64_t        max_time;                   // total time uints

    xxxx_t          stsd;                       // sample description data
    stxx_t          stts;                       // decoding time-to-samples
    stxx_t          ctts;                       // composition time-to-samples
    stxx_t          stss;                       // random seek samples
    stxx_t          stsc;                       // sample-to-chunks
    stxx_t          stsz;                       // sample sizes
    stxx_t          coxx;                       // 32/64bit chunk offsets
} stbl_t;

typedef struct {
    atom_t          atom;                       // atom structure
    xxxx_t          xmhd;                       // vmhd or smhd (video or audio media)
    stbl_t          stbl;                       // sample tables
} minf_t;

typedef struct {
    atom_t          atom;                       // atom structure
    xxhd_t          mdhd;                       // media header
    xxxx_t          hdlr;                       // media header
    minf_t          minf;                       // media information
} mdia_t;

typedef struct {
    atom_t          atom;                       // atom structure
    xxhd_t          tkhd;                       // track header
    mdia_t          mdia;                       // track media

    seek_t          start;                      // start seek information for this track
    seek_t          end;                        // end seek information for this track
} trak_t;

typedef struct {
    atom_t          atom;                       // atom structure
    xxhd_t          mvhd;                       // movie header
    trak_t          vtrak;                      // video track
    trak_t          strak;                      // sound track
} moov_t;

typedef struct {
    xxxx_t          ftyp;                       // file compatibility
    moov_t          moov;                       // movie meta data
    xxxx_t          mdat;                       // actual movie data
} file_t;

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Prepare worker for FLV parsing.
 */
int stream_mp4_setup(worker_t* worker);

/*
 * Parser function implementation for the MP4 file format.
 */
int stream_mp4_parse(stream_t* self);

/*----------------------------------------------------------------------------------------------------------*/

#endif

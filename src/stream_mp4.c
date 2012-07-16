/*
 * The Loomiere Project (http://valeriu.palos.ro/loomiere/).
 *
 * parse_mp4.c: MP4 parser.
 *
 * Read the LICENSE file!
 * Copyright (C)2010 Valeriu Paloş (valeriu@palos.ro). All rights reserved!
 */

/*----------------------------------------------------------------------------------------------------------*/

#include <math.h>
#include <string.h>
#include <unistd.h>

#include "core.h"
#include "loomiere.h"
#include "stream_mp4.h"

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Atom manipulation functions
 */
static uint32_t file_atom(stream_t* stream, atom_t* atom) {
    static uint8_t buffer[16];

    // read header (with possible extended_size)
    if (pread(stream->file, buffer, 16, stream->file_offset) != 16) {
        return ____;
    }

    // initialize
    atom->flags = 0;
    atom->size = read_32(buffer);
    atom->type = read_32(buffer + 4);
    atom->start = stream->file_offset;
    atom->data_start = atom->start + 8;

    // handle extended_size
    stream->file_offset += 8;
    if (atom->size == 1) {
        stream->file_offset += 8;
        atom->size = read_64(buffer + 8);
        atom->data_start += 8;
        atom->data_size = atom->size - 16;
        atom->flags |= F_EX;
    } else {
        atom->data_size = atom->size - 8;
    }

    // remaining fields
    atom->end = atom->start + atom->size;
    atom->data = NULL;
    atom->data_position = 0;

    return atom->type;
}

static uint32_t data_atom(atom_t* parent, atom_t* atom) {

    // sanity check
    if (!parent->data || parent->data_position > (parent->data_size - 8)) {
        return ____;
    }

    // get location in memory
    uint8_t* buffer = parent->data + parent->data_position;

    // initialize
    atom->flags = 0;
    atom->size = read_32(buffer);
    atom->type = read_32(buffer + 4);
    atom->start = parent->data_start + parent->data_position;
    atom->data_start = atom->start + 8;

    // handle extended_size
    parent->data_position += 8;
    if (atom->size == 1) {
        if (parent->data_position > (parent->data_size - 8)) {
            return ____;
        }
        parent->data_position += 8;
        atom->size = read_64(buffer + 8);
        atom->data_start += 8;
        atom->data_size = atom->size - 16;
        atom->flags |= F_EX;
    } else {
        atom->data_size = atom->size - 8;
    }

    // remaining fields
    atom->end = atom->start + atom->size;
    atom->data = parent->data + parent->data_position;
    atom->data_position = 0;
    parent->data_position += atom->data_size;

    return atom->type;
}

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Specialized initializer functions
 */
static void init_stxx(stxx_t* stxx) {
    if (VOID(*stxx)) return;
    stxx->version = stxx->atom.data[0];
    stxx->flags   = read_24(&stxx->atom.data[1]);
    stxx->count   = read_32(&stxx->atom.data[4]);
    stxx->data    = &stxx->atom.data[8];
}

static void init_stsz(stxx_t* stsz) {
    stsz->version = stsz->atom.data[0];
    stsz->flags   = read_24(&stsz->atom.data[1]);
    stsz->size    = read_32(&stsz->atom.data[4]);
    stsz->count   = stsz->size ? 0 : read_32(&stsz->atom.data[8]);
    stsz->data    = stsz->size ? 0 : &stsz->atom.data[12];
}

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Format parsing functions.
 */
static int parse_stbl(stream_t* stream, stbl_t* stbl) {
    atom_t atom;
    int left = 7;

    // parse forth level atoms
    stbl->atom.data_position = 0;
    while (left > 0 && stbl->atom.data_position < stbl->atom.data_size) {
        switch (data_atom(&stbl->atom, &atom)) {
        case STSD: stbl->stsd.atom = atom; left--; break;
        case STTS: stbl->stts.atom = atom; stbl->stts.bytes =  8; left--; break;
        case CTTS: stbl->ctts.atom = atom; stbl->ctts.bytes =  8; left--; break;
        case STSS: stbl->stss.atom = atom; stbl->stss.bytes =  4; left--; break;
        case STSC: stbl->stsc.atom = atom; stbl->stsc.bytes = 12; left--; break;
        case STSZ: stbl->stsz.atom = atom; stbl->stsz.bytes =  4; left--; break;
        case STCO: stbl->coxx.atom = atom; stbl->coxx.bytes =  4; left--; break;
        case CO64: stbl->coxx.atom = atom; stbl->coxx.bytes =  8; left--; break;
        }
    }

    // sanity checks
    if (VOID(stbl->stts)) return 1;                                             // missing STTS
    if (VOID(stbl->stsc)) return 1;                                             // missing STSC
    if (VOID(stbl->stsz)) return 1;                                             // missing STSZ
    if (VOID(stbl->coxx)) return 1;                                             // missing STCO/CO64

    // initialize tables
    init_stxx(&stbl->stts);
    init_stxx(&stbl->ctts);
    init_stxx(&stbl->stss);
    init_stxx(&stbl->stsc);
    init_stsz(&stbl->stsz);
    init_stxx(&stbl->coxx);

    // success
    return 0;
}

static int parse_minf(stream_t* stream, minf_t* minf) {
    atom_t atom;
    int left = 2;

    // parse forth level atoms
    minf->atom.data_position = 0;
    while (left > 0 && minf->atom.data_position < minf->atom.data_size) {
        switch (data_atom(&minf->atom, &atom)) {
        case VMHD:
        case SMHD: minf->xmhd.atom = atom; left--; break;
        case STBL: minf->stbl.atom = atom; left--; if (parse_stbl(stream, &minf->stbl)) return 1; break;
        }
    }

    // sanity checks
    if (!VOID(minf->xmhd) && VOID(minf->stbl)) return 1;                        // missing STBL in a/v TRAK

    // success
    return 0;
}

static int parse_mdia(stream_t* stream, mdia_t* mdia) {
    atom_t atom;
    int left = 3;

    // parse forth level atoms
    mdia->atom.data_position = 0;
    while (left > 0 && mdia->atom.data_position < mdia->atom.data_size) {
        switch (data_atom(&mdia->atom, &atom)) {
        case MDHD: mdia->mdhd.atom = atom; left--; break;
        case HDLR: mdia->hdlr.atom = atom; left--; break;
        case MINF: mdia->minf.atom = atom; left--; if (parse_minf(stream, &mdia->minf)) return 1; break;
        }
    }

    // sanity checks
    if (VOID(mdia->mdhd)) return 1;                                             // missing MDHD

    // initialize header
    mdia->mdhd.version  = mdia->mdhd.atom.data[0];
    int offset          = mdia->mdhd.version ? 20 : 12;
    mdia->mdhd.scale    = read_32(&mdia->mdhd.atom.data[offset]);
    uint8_t* p          = &mdia->mdhd.atom.data[offset + 4];
    mdia->mdhd.duration = mdia->mdhd.version ? read_64(p) : read_32(p);

    // success
    return 0;
}

static int parse_trak(stream_t* stream, trak_t* trak) {
    atom_t atom;
    int left = 2;

    // parse third level atoms
    trak->atom.data_position = 0;
    while (left > 0 && trak->atom.data_position < trak->atom.data_size) {
        switch (data_atom(&trak->atom, &atom)) {
        case TKHD: trak->tkhd.atom = atom; left--; break;
        case MDIA: trak->mdia.atom = atom; left--; if (parse_mdia(stream, &trak->mdia)) return 1; break;
        }
    }

    // sanity checks
    if (VOID(trak->tkhd)) return 1;                                             // missing TKHD
    if (VOID(trak->mdia)) return 1;                                             // missing MDIA

    // initialize header
    trak->tkhd.version  = trak->tkhd.atom.data[0];
    trak->tkhd.flags    = read_24(&trak->tkhd.atom.data[1]);
    uint8_t* p          = &trak->tkhd.atom.data[trak->tkhd.version ? 28 : 20];
    trak->tkhd.duration = trak->tkhd.version ? read_64(p) : read_32(p);

    // success
    return 0;
}

static int parse_moov(stream_t* stream, moov_t* moov) {
    atom_t atom;
    trak_t trak;
    int left = 3;

    // parse second level atoms
    moov->atom.data_position = 0;
    while (left > 0 && moov->atom.data_position < moov->atom.data_size) {
        switch (data_atom(&moov->atom, &atom)) {
        case MVHD: moov->mvhd.atom = atom; left--; break;
        case TRAK:
            ZERO(&trak, sizeof(trak_t));
            trak.atom = atom;
            if (parse_trak(stream, &trak)) {
                return 1;
            }
            if (!(trak.tkhd.flags & 0x000001)) {
                break;
            }
            switch (trak.mdia.minf.xmhd.atom.type) {
                case VMHD: if (VOID(moov->vtrak)) { moov->vtrak = trak; left--; } break;
                case SMHD: if (VOID(moov->strak)) { moov->strak = trak; left--; } break;
            }
            break;
        case CMOV:
            return 1;                                                           // CMOV not supported!
        break;
        }
    }

    // sanity checks
    if (VOID(moov->mvhd)) return 1;                                             // missing MVHD
    if (VOID(moov->vtrak) && VOID(moov->strak)) return 1;                       // missing valid a/v TRAKs

    // initialize header
    moov->mvhd.version  = moov->mvhd.atom.data[0];
    int offset          = moov->mvhd.version ? 20 : 12;
    moov->mvhd.scale    = read_32(&moov->mvhd.atom.data[offset]);
    uint8_t* p          = &moov->mvhd.atom.data[offset + 4];
    moov->mvhd.duration = moov->mvhd.version ? read_64(p) : read_32(p);

    // clear preview/select times
    offset += 52 + (moov->mvhd.version ? 12 : 8);
    moov->mvhd.atom.data[offset] = 0;
    memset(&moov->mvhd.atom.data[offset], 0, 24);

    // success
    return 0;
}

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Seek compilation functions.
 */
static void compile_maxs(stbl_t* stbl) {
    uint32_t i, c;
    uint8_t* p = stbl->stts.data;

    // initialize
    stbl->max_samples = 0;
    stbl->max_time = 0;

    // sample, time
    for (i = 0; i < stbl->stts.count; i++, p += 8) {
        c = read_32(&p[0]);                                                     // sample count
        stbl->max_samples += c;                                                 // samples
        stbl->max_time += c * read_32(&p[4]);                                   // time
    }

    // chunk
    stbl->max_chunks = stbl->coxx.count;

    // offset
    c = read_32(&stbl->stsc.data[(stbl->stsc.count - 1) * 12 + 4]);             // sample count
    stbl->max_offset = read_xx(&stbl->coxx.data[(stbl->coxx.count - 1) *        // last chunk offset
                                                 stbl->coxx.bytes],
                               stbl->coxx.bytes << 3);
    for (i = 0; i < c; i++) {
        stbl->max_offset += stbl->stsz.size ? stbl->stsz.size :
                            read_32(&stbl->stsz.data[(stbl->stsz.count - i - 1) << 2]);
    }
}

static void compile_seek(stbl_t* stbl, seek_t* seek) {
    uint32_t i;
    uint32_t t, u, c, d, n;
    uint8_t* p;

    // look-up stts
    p = stbl->stts.data;
    seek->stts.index = 0;
    for (u = 0, c = 0, t = 0, n = 0;
         seek->stts.index < stbl->stts.count;
         seek->stts.index++, p += 8) {

        c  = read_32(&p[0]);                                                    // read count
        d  = read_32(&p[4]);                                                    // read duration
        u  = c * d;                                                             // whole entry time
        if ((t + u) > seek->time) break;                                        // break if passed
        n += c;                                                                 // increment count
        t += u;                                                                 // increment time
        d  = 1;
    }
    seek->stts.offset  = (seek->time - t) / d;                                  // save offset
    seek->time         = MIN(t + seek->stts.offset * d, stbl->max_time);        // save sample time
    seek->stsz.index   = MIN(n + seek->stts.offset, stbl->max_samples);         // save sample number

    // look-up stss (if available) and snap to keyframe
    seek->stss.index = 0;                                                       // first keyframe
    if (!VOID(stbl->stss)) {
        t = seek->stsz.index;                                                   // initial sample number

        if (t < stbl->stsz.count) {
            p = stbl->stss.data + 4;                                            // second keyframe
            seek->stsz.index = 0;
            seek->stss.index = 1;
            for (n = 0;
                 seek->stss.index < stbl->stss.count;
                 seek->stss.index++, p += 4) {

                n = read_32(&p[0]) - 1;                                         // read new sample id
                if (n > t) {                                                    // went too far?
                    seek->stss.index--;                                         // devance index
                    break;
                }
                seek->stsz.index = n;                                           // correct sample number
            }
        } else {
            seek->stss.index = stbl->stss.count;
        }

        // correct stts offsets
        t -= seek->stsz.index;
        d  = seek->stts.offset ?
             read_32(&stbl->stts.data[(seek->stts.index << 3) + 4]) : 0;        // read duration
        while (t > 0) {
            if (seek->stts.offset) {
                seek->stts.offset--;                                            // decrease offset
            } else {
                p = &stbl->stts.data[--seek->stts.index << 3];                  // decrease index
                seek->stts.offset = read_32(&p[0]) - 1;                         // read count
                d = read_32(&p[4]);                                             // read duration
            }
            seek->time -= d;                                                    // decrease duration
            t--;                                                                // decrease step
        }
    }

    // look-up ctts (if available)
    if (!VOID(stbl->ctts)) {
        p = stbl->ctts.data;
        seek->ctts.index = 0;
        for (i = 0, n = 0;
             seek->ctts.index < stbl->ctts.count;
             seek->ctts.index++, p += 8) {
            c = read_32(&p[0]);                                                 // read count
            d = read_32(&p[4]);                                                 // read time offset
            if ((n + c) > seek->stsz.index) break;                              // went too far?
            n += c;                                                             // increment count
        }
        seek->ctts.offset = seek->stsz.index - n;                               // save offset
    }

    // look-up stsc
    p = stbl->stsc.data;
    seek->stsc.index = 0;
    seek->coxx.index = 0;                                                       // first chunk number
    for (n = 0;
         seek->stsc.index < stbl->stsc.count;
         seek->stsc.index++, p += 12) {
        c = read_32(&p[4]);                                                     // read samples per chunk
        d = (seek->stsc.index == (stbl->stsc.count - 1)) ?                      // read next chunk number
             stbl->max_chunks : (read_32(&p[12]) - 1);
        u = d - seek->coxx.index;                                               // chunks in this entry
        d = u * c;                                                              // samples in this entry
        if ((n + d) > seek->stsz.index) break;                                  // went too far?
        n += d;                                                                 // increment samples
        seek->coxx.index += u;                                                  // increment chunks
        c = 1;
    }
    d = seek->stsz.index - n;                                                   // sample within entry
    seek->stsc.offset = d / c;                                                  // save offset
    seek->coxx.index += seek->stsc.offset;                                      // save chunk number
    seek->coxx.offset = d % c;                                                  // sample within chunk

    // look-up stco/co64
    if (seek->coxx.index < stbl->max_chunks) {
        seek->offset = read_xx(&stbl->coxx.data[seek->coxx.index *              // read offset from array
                                                stbl->coxx.bytes],
                                stbl->coxx.bytes << 3);
    } else {
        seek->offset = stbl->max_offset;                                        // use end-of-data
    }

    // look-up stsz
    if (stbl->stsz.size) {
        seek->offset += seek->coxx.offset * stbl->stsz.size;
    } else if (seek->coxx.offset) {
        n = seek->coxx.offset;
        while (n) {
            seek->offset += read_32(&stbl->stsz.data[(seek->stsz.index - n) << 2]);
            n--;
        }
    }
}

static void resize_xxxx(stxx_t* xxxx, tbli_t* start, tbli_t* end, tbli_t* end2) {
    uint8_t  x = (end->offset > 0) || (end2 && (end2->offset > 0));             // include last entry?
    uint64_t a = start->index * xxxx->bytes;                                    // advance delta
    uint64_t d = (xxxx->count - end->index - x) * xxxx->bytes;                  // devance delta

    xxxx->count = end->index - start->index + x;                                // entries count
    xxxx->data += a;                                                            // advance entries start

    xxxx->atom.size -= a + d;                                                   // atom size
    xxxx->atom.start += a;                                                      // advance start
    xxxx->atom.end -= d;                                                        // devance end
    xxxx->atom.data += a;                                                       // advance data pointer
    xxxx->atom.data_start += a;                                                 // advance data start
    xxxx->atom.data_size = 8 + (xxxx->count * xxxx->bytes);                     // atom data size
    xxxx->atom.data_position = 0;                                               // reset cursor
}

static void compile_xxxx(stxx_t* xxxx, tbli_t* start, tbli_t* end, tbli_t* end2) {
    if (VOID(*xxxx)) return;
    resize_xxxx(xxxx, start, end, end2);                                        // resize
    write_32(&xxxx->atom.data[0], xxxx->version << 24 | xxxx->flags);           // write version/flags
    write_32(&xxxx->atom.data[4], xxxx->count);                                 // write count
}

static void compile_stsz(stxx_t* stsz, tbli_t* start, tbli_t* end, tbli_t* end2) {
    if (stsz->size) return;
    resize_xxxx(stsz, start, end, end2);                                        // resize
    stsz->atom.data_size += 4;
    write_32(&stsz->atom.data[0], stsz->version << 24 | stsz->flags);           // write version/flags
    write_32(&stsz->atom.data[4], 0);                                           // write sample size
    write_32(&stsz->atom.data[8], stsz->count);                                 // write count
}

static void clip_xtts(stxx_t* xtts, uint32_t offset_start, uint32_t offset_end) {
    if (VOID(*xtts)) return;

    uint32_t _offset_a = 0;
    uint32_t _offset_b = xtts->count ? (xtts->count - 1) << 3 : _offset_a;
    uint32_t _count_a = read_32(&xtts->data[_offset_a]);
    uint32_t _count_b = read_32(&xtts->data[_offset_b]);

    if (offset_start) {
        write_32(&xtts->data[_offset_a], _count_a - offset_start);
    }
    if (offset_end) {
        write_32(&xtts->data[_offset_b], read_32(&xtts->data[_offset_b]) - (_count_b - offset_end));
    }
}

static void clip_stsc(stbl_t* stbl, seek_t* start, seek_t* end) {
    if (VOID(stbl->stsc)) return;

    // correct initial chunk offset
    write_xx(&stbl->coxx.data[0], start->offset, stbl->coxx.bytes << 3);

    // assess situation
    uint32_t s  = stbl->stsc.count;
    uint8_t* p  = &stbl->stsc.data[0];                                          // table start
    uint32_t na = read_32(p) - 1;                                               // first chunk id
    uint32_t nb = (s == 1) ? stbl->max_chunks : (read_32(p + 12) - 1);          // next chunk id
    uint32_t n  = nb - (na + start->stsc.offset);                               // chunks left in entry
    uint32_t c  = read_32(&p[4]);                                               // samples per chunk
    uint32_t i;

    // prepend compensation entry if needed
    if (start->coxx.offset) {

        // prepare first entry
        write_32(&start->stsc_entry[0], 1);                                     // first chunk id
        write_32(&start->stsc_entry[4], c - start->coxx.offset);                // samples per chunk
        write_32(&start->stsc_entry[8], read_32(&p[8]));                        // sample description id
        stbl->stsc.atom.size += 12;                                             // adjust atom size
        stbl->stsc.count++;                                                     // adjust count
        start->coxx.offset = 1;                                                 // activate iovs

        // prepare second entry
        if (n > 1) {                                                            // many chunks in entry
            write_32(p, 2);                                                     // second chunk id
        } else {                                                                // single chunk in entry
            stbl->stsc.count--;
            stbl->stsc.atom.size -= 12;
            for (i = 0; i < 12; i++) {                                          // copy entire stsc_entry
                p[i] = start->stsc_entry[i];
            }
            start->coxx.offset = 0;                                             // deactivate extra entry
        }
    } else {                                                                    // sample is chunk-aligned
        write_32(p, 1);                                                         // first chunk id
    }
    p += 12;                                                                    // advance to next entry
    n++;                                                                        // third chunk id

    // prepare remaining entries
    if (s > 1) {
        uint32_t d = read_32(p) - 1 - n;                                        // chunk delta
        for (i = start->stsc.index + 1;
             i < end->stsc.index + (end->stsc.offset > 0 || end->coxx.offset > 0);
             i++, p += 12) {
            n = read_32(p) - 1 - d;                                             // shift chunk id
            write_32(p, n);
        }
        p -= 12;
    }

    // append compensation entry if needed
    if (end->coxx.offset) {
        write_32(&end->stsc_entry[0], stbl->coxx.count);                        // last chunk id
        write_32(&end->stsc_entry[4], end->coxx.offset);                        // samples per chunk
        write_32(&end->stsc_entry[8], read_32(&p[8]));                          // sample description id
        stbl->stsc.atom.size += 12;                                             // adjust atom size
        stbl->stsc.count++;                                                     // adjust count
        end->coxx.offset = 1;                                                   // activate iovs
    }

    // update atom meta-information
    write_32(&stbl->stsc.atom.data[0], stbl->stsc.version << 24 | stbl->stsc.flags);
    write_32(&stbl->stsc.atom.data[4], stbl->stsc.count);
}

static void resize_atom(atom_t* a) {
    a->size = a->data_size + (Q_EX(*a) ? 16 : 8);
}

static void write_time(xxhd_t* xxhd, uint32_t pos32, uint32_t pos64) {
    write_xx(&xxhd->atom.data[xxhd->version ? pos64 : pos32], xxhd->duration, xxhd->version ? 64 : 32);
}

static void compile_trak(stream_t* stream, file_t* file, trak_t* trak) {
    if (VOID(*trak)) return;

    // get requested times from stream
    trak->start.time = (uint64_t)(stream->start * (double)trak->mdia.mdhd.scale);
    trak->end.time = (uint64_t)(stream->stop * (double)trak->mdia.mdhd.scale);

    // sanity checks
    if (trak->mdia.mdhd.duration > trak->mdia.minf.stbl.max_time) {
        trak->mdia.mdhd.duration = trak->mdia.minf.stbl.max_time;
    }
    if (trak->start.time > trak->mdia.mdhd.duration) {
        trak->start.time = trak->mdia.mdhd.duration;
    }
    if (trak->end.time == 0 || trak->end.time > trak->mdia.mdhd.duration) {
        trak->end.time = trak->mdia.mdhd.duration;
        trak->end.offset = trak->mdia.minf.stbl.max_offset;
    }

    // compile seek points
    compile_seek(&trak->mdia.minf.stbl, &trak->start);
    compile_seek(&trak->mdia.minf.stbl, &trak->end);

    // adjust media timeline
    trak->mdia.mdhd.duration = trak->end.time - trak->start.time;
    write_time(&trak->mdia.mdhd, 16, 24);

    // adjust track timeline
    trak->tkhd.duration = (uint64_t)ROUND((double)file->moov.mvhd.scale *
                                         ((double)trak->mdia.mdhd.duration /
                                          (double)trak->mdia.mdhd.scale));
    write_time(&trak->tkhd, 20, 28);

    // adjust stream offsets
    if (!stream->file_offset || stream->file_offset > trak->start.offset) {
        stream->start = (double)trak->start.time / (double)trak->mdia.mdhd.scale;
        stream->file_offset = trak->start.offset;
    }
    if (!stream->file_finish || stream->file_finish < trak->end.offset) {
        stream->stop = (double)trak->end.time / (double)trak->mdia.mdhd.scale;
        stream->file_finish = trak->end.offset;
        //stream->stop += stream->stop ? 0 : 1;
    }

    // restructuring stbl
    stbl_t* _stbl  = &trak->mdia.minf.stbl;
    compile_xxxx(&_stbl->stts, &trak->start.stts, &trak->end.stts, NULL);
    compile_xxxx(&_stbl->ctts, &trak->start.ctts, &trak->end.ctts, NULL);
    compile_xxxx(&_stbl->stss, &trak->start.stss, &trak->end.stss, NULL);
    compile_stsz(&_stbl->stsz, &trak->start.stsz, &trak->end.stsz, NULL);
    compile_xxxx(&_stbl->coxx, &trak->start.coxx, &trak->end.coxx, NULL);

    // clip regular stbl tables accurately
    clip_xtts(&_stbl->stts, trak->start.stts.offset, trak->end.stts.offset);
    clip_xtts(&_stbl->ctts, trak->start.ctts.offset, trak->end.ctts.offset);

    // compile stsc table
    resize_xxxx(&_stbl->stsc, &trak->start.stsc, &trak->end.stsc, &trak->end.coxx);
    clip_stsc(_stbl, &trak->start, &trak->end);

    // resize stbl
    _stbl->atom.data_size = _stbl->stsd.atom.size + _stbl->stts.atom.size + _stbl->ctts.atom.size +
                            _stbl->stss.atom.size + _stbl->stsz.atom.size + _stbl->stsc.atom.size +
                            _stbl->coxx.atom.size;
    resize_atom(&_stbl->atom);

    // resize minf
    trak->mdia.minf.atom.data_size = trak->mdia.minf.xmhd.atom.size +
                                     trak->mdia.minf.stbl.atom.size;
    resize_atom(&trak->mdia.minf.atom);

    // resize mdia
    trak->mdia.atom.data_size = trak->mdia.mdhd.atom.size +
                                trak->mdia.hdlr.atom.size +
                                trak->mdia.minf.atom.size;
    resize_atom(&trak->mdia.atom);

    // resize trak
    trak->atom.data_size = trak->tkhd.atom.size +
                           trak->mdia.atom.size;
    resize_atom(&trak->atom);
}

static void compile_moov(stream_t* stream, file_t* file) {
    // resize moov
    file->moov.atom.data_size = file->moov.mvhd.atom.size +
                                file->moov.vtrak.atom.size +
                                file->moov.strak.atom.size;

    // include header
    resize_atom(&file->moov.atom);

    // ajust movie timeline (longest track)
    file->moov.mvhd.duration = MAX(file->moov.strak.tkhd.duration,
                                   file->moov.vtrak.tkhd.duration);

    write_time(&file->moov.mvhd, 16, 24);
}

static void compile_mdat(stream_t* stream, file_t* file) {
    // resize moov
    file->mdat.atom.data_size = stream->file_finish - stream->file_offset;

    // make size non-zero
    file->mdat.atom.size = file->mdat.atom.data_size;

    // include header
    resize_atom(&file->mdat.atom);
}

static void relocate_trak(stream_t* stream, file_t* file, trak_t* trak, uint64_t start) {
    if (VOID(*trak)) return;

    uint64_t i;
    int64_t  delta;
    uint8_t* p;

    // shift sync samples (stss)
    if (!VOID(trak->mdia.minf.stbl.stss)) {
        p = trak->mdia.minf.stbl.stss.data;
        delta = read_32(&p[0]) - 1;

        i = trak->mdia.minf.stbl.stss.count;
        for (; i > 0; i--, p += 4) {
            write_32(p, read_32(p) - delta);
        }
    }

    // compute delta
    delta = stream->file_offset - start;

    // translate
    uint8_t _bits = trak->mdia.minf.stbl.coxx.bytes << 3;
    i = trak->mdia.minf.stbl.coxx.count;
    p = trak->mdia.minf.stbl.coxx.data;
    for (; i > 0; i--, p += trak->mdia.minf.stbl.coxx.bytes) {
        write_xx(p, (int64_t)read_xx(p, _bits) - delta, _bits);
    }
}

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Metadata generation functions.
 */
static void iovs_head(iovs_t* i, xxxx_t* s, uint8_t** heads) {
    if (VOID(*s)) return;

    // prepare data
    size_t _hs = Q_EX(s->atom) ? 16 : 8;
    write_32(*heads + 4, s->atom.type);
    if (_hs > 8) {
        write_32((*heads) + 0, 1);
        write_64((*heads) + 8, s->atom.size);
    } else {
        write_32((*heads) + 0, s->atom.size);
    }

    // register vector
    i->iovs[i->count].base = *heads;
    i->iovs[i->count].size = _hs;
    i->count++;
    i->size += _hs;
    *heads = (*heads) + _hs;
}

static void iovs_full(iovs_t* i, xxxx_t* s, uint8_t** heads) {
    if (VOID(*s)) return;

    // head
    iovs_head(i, s, heads);

    // data
    i->iovs[i->count].base = s->atom.data;
    i->iovs[i->count].size = s->atom.data_size;
    i->count++;
    i->size += s->atom.data_size;
}

static void iovs_stsc(iovs_t* i, stxx_t* s, seek_t* start, seek_t* end) {
    if (VOID(*s)) return;

    // pre-bulk data (version, flags, count)
    i->iovs[i->count].base = s->atom.data;
    i->iovs[i->count].size = 8;
    i->count++;
    i->size += 8;
    s->atom.data += 8;
    s->atom.data_size -= 8;

    // prepending additional stsc entry
    if (start->coxx.offset) {
        i->iovs[i->count].base = start->stsc_entry;
        i->iovs[i->count].size = 12;
        i->count++;
        i->size += 12;
    }

    // bulk data
    i->iovs[i->count].base = s->atom.data;
    i->iovs[i->count].size = s->atom.data_size;
    i->count++;
    i->size += s->atom.data_size;

    // prepending additional stsc entry
    if (end->coxx.offset) {
        i->iovs[i->count].base = end->stsc_entry;
        i->iovs[i->count].size = 12;
        i->count++;
        i->size += 12;
    }
}

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Delivery functions.
 */
static void compile_head(stream_t* self, file_t* file) {

    // initialize
    int i;
    iovs_t _iovs;
    ZERO(&_iovs, sizeof(iovs_t));

    // initialize atom header buffers
    uint8_t  _heads_base[70 * 16];                                              // assume 70 16-byte heads
    uint8_t* _heads = _heads_base;

    // gather deta atoms
    iovs_full(&_iovs, (xxxx_t*)&file->ftyp, &_heads);                           // file compatibility
    iovs_head(&_iovs, (xxxx_t*)&file->moov, &_heads);                           // movie metadata
    iovs_full(&_iovs, (xxxx_t*)&file->moov.mvhd, &_heads);                      // metadata header

    // gather tracks
    trak_t* ts[2];
    ts[0] = &file->moov.vtrak;                                                  // video track
    ts[1] = &file->moov.strak;                                                  // audio track
    for (i = 0; i < 2; i++) {
        if (!VOID(*ts[i])) {
            iovs_head(&_iovs, (xxxx_t*)ts[i], &_heads);                         // track box
            iovs_full(&_iovs, (xxxx_t*)&ts[i]->tkhd, &_heads);                  // track header

            mdia_t* mdia = &ts[i]->mdia;                                        // mdia alias
            iovs_head(&_iovs, (xxxx_t*)mdia, &_heads);                          // media box
            iovs_full(&_iovs, (xxxx_t*)&mdia->mdhd, &_heads);                   // media header
            iovs_full(&_iovs, (xxxx_t*)&mdia->hdlr, &_heads);                   // media header

            minf_t* minf = &ts[i]->mdia.minf;                                   // minf alias
            iovs_head(&_iovs, (xxxx_t*)minf, &_heads);                          // media information box
            iovs_full(&_iovs, (xxxx_t*)&minf->xmhd, &_heads);                   // video media header

            stbl_t* stbl = &minf->stbl;                                         // stbl alias
            iovs_head(&_iovs, (xxxx_t*)stbl, &_heads);                          // samples tables
            iovs_full(&_iovs, (xxxx_t*)&stbl->stsd, &_heads);                   // samples description table
            iovs_full(&_iovs, (xxxx_t*)&stbl->stts, &_heads);                   // decoding time-to-sample
            iovs_full(&_iovs, (xxxx_t*)&stbl->stss, &_heads);                   // sync sample table
            iovs_head(&_iovs, (xxxx_t*)&stbl->stsc, &_heads);                   // sample-to-chunk
            iovs_stsc(&_iovs, (stxx_t*)&stbl->stsc,                             // stsc with extra entries
                               &ts[i]->start, &ts[i]->end);
            iovs_full(&_iovs, (xxxx_t*)&stbl->ctts, &_heads);                   // composition offsets
            iovs_full(&_iovs, (xxxx_t*)&stbl->stsz, &_heads);                   // sample sizes
            iovs_full(&_iovs, (xxxx_t*)&stbl->coxx, &_heads);                   // 32bit chunk offsets
        }
    }

    // gather media data
    iovs_head(&_iovs, (xxxx_t*)&file->mdat, &_heads);                           // media data

    // relocate sample chunks
    relocate_trak(self, file, &file->moov.vtrak, _iovs.size);                   // video trak
    relocate_trak(self, file, &file->moov.strak, _iovs.size);                   // sound trak

    // generate HTTP headers
    char* head = FORMAT("HTTP/%s 200 OK\n"
                        "Content-Type: %s\n"
                        "Content-Length: %llu\n"
                        "Cache-Control: no-store, no-cache, must-revalidate, post-check=0, pre-check=0\n"
                        "Expires: Mon, 29 Mar 1982 12:00:00 GMT\n"
                        "Server: %s %s\n\n",
                        self->http, STREAM_MP4_MIME,
                        (unsigned long long)(self->file_finish - self->file_offset + _iovs.size),
                        ID_NAME, ID_VERSION);
    size_t head_length = strlen(head);

    // prepare headers
    self->head_offset = 0;
    self->head_length = head_length + _iovs.size;
    self->head = (char*)ALLOC(self->head_length);
    memcpy(self->head, head, head_length);
    FREE(head);
    head = self->head + head_length;

    // assemble headers
    for (i = 0; i < _iovs.count; i++) {
        memcpy(head, _iovs.iovs[i].base, _iovs.iovs[i].size);
        head += _iovs.iovs[i].size;
    }
}

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Miscellaneous macros.
 */
#define _SAVE_ATOM(name)    name = (char*)ALLOC(atom.size); \
                            name##_size = atom.size; \
                            if (pread(self->file, name, atom.size, atom.start) != atom.size) { \
                                goto error; \
                            }

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Prepare worker for FLV parsing.
 */
int stream_mp4_setup(worker_t* worker) {
    return 0;
}

/*
 * Parser function implementation for the MP4 file format.
 */
int stream_mp4_parse(stream_t* self) {

    // exit code
    int status = 0;

    // initialize
    file_t file;
    ZERO(&file, sizeof(file_t));

    // offsets cache key
    char* okey_name = FORMAT("%s:offsets", self->path);
    int   okey_size = strlen(okey_name);

    // zero-seek cache keys
    char* hkey_name = FORMAT("%s:zero:head", self->path);
    int   hkey_size = strlen(hkey_name);
    char* lkey_name = FORMAT("%s:zero:limits", self->path);
    int   lkey_size = strlen(lkey_name);

    // generational cache keys
    char* ftyp_key_name = NULL;
    int   ftyp_key_size = 0;
    char* moov_key_name = NULL;
    int   moov_key_size = 0;
    char* mdat_key_name = NULL;
    int   mdat_key_size = 0;

    // main atoms (from cache)
    char* ftyp = NULL;
    int   ftyp_size = 0;
    char* moov = NULL;
    int   moov_size;
    char* mdat = NULL;
    int   mdat_size;

    // attempt zero-seek
    int periods;
    self->offsets = self->db ? tcadbget(self->db, okey_name, okey_size, &periods) : NULL;
    self->periods = periods / sizeof(off_t);

    // perform zero-seek
    if (self->offsets && !self->start && !self->stop) {

        // zero-head
        int length = 0;
        self->head = tcadbget(self->db, hkey_name, hkey_size, &length);
        self->head_length = length;

        // zero-limits
        int limits_size = 0;
        off_t* limits = tcadbget(self->db, lkey_name, lkey_size, &limits_size);
        if (self->head && limits) {
            self->file_offset = limits[0];
            self->file_finish = limits[1];
        } else {
            FREE(self->head);
        }
        FREE(limits);
    }

    // regenerate
    if (self->head) {

        // count
        (*self->cache_hits)++;

    } else {

        // generate keys
        ftyp_key_name = FORMAT("%s:atom:ftyp", self->path);
        ftyp_key_size = strlen(ftyp_key_name);
        moov_key_name = FORMAT("%s:atom:moov", self->path);
        moov_key_size = strlen(moov_key_name);
        mdat_key_name = FORMAT("%s:atom:mdat", self->path);
        mdat_key_size = strlen(mdat_key_name);

        // get stored data
        ftyp = self->db ? tcadbget(self->db, ftyp_key_name, ftyp_key_size, &ftyp_size) : NULL;
        moov = self->db ? tcadbget(self->db, moov_key_name, moov_key_size, &moov_size) : NULL;
        mdat = self->db ? tcadbget(self->db, mdat_key_name, mdat_key_size, &mdat_size) : NULL;

        // reload meta-data
        atom_t atom;
        if (!moov || !mdat) {

            // count
            (*self->cache_misses)++;

            // parse first level atoms
            int left = 3;
            self->file_offset = 0;
            while (left > 0 && self->file_offset < self->file_length) {
                switch (file_atom(self, &atom)) {
                case FTYP: _SAVE_ATOM(ftyp); left--; break;
                case MDAT: atom.size = atom.data_start - atom.start;
                           _SAVE_ATOM(mdat); if (!mdat) left--; break;
                case MOOV: _SAVE_ATOM(moov); left--; break;
                case ____: goto error; break;
                }
                self->file_offset = atom.end;
            }

            // sanity checks
            if (!moov) goto error;                                              // missing MOOV
            if (!mdat) goto error;                                              // missing MDAT

            // store in cache
            if (self->db) {
                if (ftyp) {
                    tcadbput(self->db, ftyp_key_name, ftyp_key_size, ftyp, ftyp_size);
                }
                tcadbput(self->db, moov_key_name, moov_key_size, moov, moov_size);
                tcadbput(self->db, mdat_key_name, mdat_key_size, mdat, mdat_size);
            }

        } else {

            // count
            (*self->cache_hits)++;
        }

        // map ftyp (if available)
        if (ftyp) {
            atom.data = ftyp;
            atom.data_size = ftyp_size;
            atom.data_position = 0;
            if (data_atom(&atom, &file.ftyp.atom) == ____) {
                ZERO(&file.ftyp, sizeof(xxxx_t));
            }
        }

        // map moov
        atom.data = moov;
        atom.data_size = moov_size;
        atom.data_position = 0;
        if (data_atom(&atom, &file.moov.atom) == ____) {
            goto error;
        }

        // map mdat
        atom.data = mdat;
        atom.data_size = mdat_size;
        atom.data_position = 0;
        if (data_atom(&atom, &file.mdat.atom) == ____) {
            goto error;
        }

        // parse meta-data
        if (parse_moov(self, &file.moov)) {
            goto error;
        }

        // get duration (periods)
        if (!file.moov.mvhd.scale) goto error;
        self->periods = ceil((double)file.moov.mvhd.duration / (double)file.moov.mvhd.scale);
        if (!self->periods) goto error;

        // compile limits
        if (!VOID(file.moov.vtrak)) compile_maxs(&file.moov.vtrak.mdia.minf.stbl);
        if (!VOID(file.moov.strak)) compile_maxs(&file.moov.strak.mdia.minf.stbl);

        // regenerate offsets (if required)
        if (!self->offsets) {

            // choose track
            trak_t* trak = VOID(file.moov.vtrak) ? &file.moov.strak : &file.moov.vtrak;

            // convert period
            uint32_t period = self->period * trak->mdia.mdhd.scale;

            // shortcut
            stbl_t*  stbl = &trak->mdia.minf.stbl;

            // cursors
            uint32_t i;
            uint8_t* p;
            uint32_t u = 0, d = 0, c = 0;
            uint32_t t = 0, n = 0, k = 0;

            uint32_t time = 0;
            tbli_t   sample = {0, 0};
            uint32_t sample_id = 0;
            tbli_t   chunk = {0, 0};
            uint32_t chunk_last = 0;
            uint32_t chunk_id = 0;
            uint32_t chunk_sample = 0;

            uint32_t offset;

            // allocate space
            self->offsets = (off_t*)ALLOC(sizeof(off_t) * self->periods);

            // walk space-time
            for (i = 0; i < self->periods; i++, time += period) {

                // find sample number
                p = stbl->stts.data + 8 * sample.index;
                u = d = c = 0;
                for (; sample.index < stbl->stts.count; sample.index++, p += 8) {
                    c  = read_32(&p[0]);                                        // read count
                    d  = read_32(&p[4]);                                        // read duration
                    u  = c * d;                                                 // whole entry time
                    if ((t + u) > time) break;                                  // break if passed
                    n += c;                                                     // increment count
                    t += u;                                                     // increment time
                    d  = 1;
                }
                if (!d) {
                    sample_id     = stbl->max_samples;
                } else {
                    sample.offset = (time - t) / d;                             // save offset
                    sample_id     = MIN(n + sample.offset, stbl->max_samples);  // save sample number
                }

                // find chunk number
                p = stbl->stsc.data + 12 * chunk.index;
                u = d = c = 0;
                for (; chunk.index < stbl->stsc.count; chunk.index++, p += 12) {
                    c = read_32(&p[4]);                                         // read samples per chunk
                    d = (chunk.index == (stbl->stsc.count - 1)) ?               // read next chunk number
                         stbl->max_chunks : (read_32(&p[12]) - 1);
                    u = d - chunk_last;                                         // chunks in this entry
                    d = u * c;                                                  // samples in this entry
                    if ((k + d) > sample_id) break;                             // went too far?
                    k += d;                                                     // increment samples
                    chunk_last += u;                                            // increment chunks
                    c = 1;
                }
                if (!c) {
                    chunk_id = stbl->max_chunks;
                    chunk_sample = 0;
                } else {
                    d = sample_id - k;                                          // sample within entry
                    chunk.offset = d / c;                                       // save offset
                    chunk_id = chunk_last + chunk.offset;                       // save chunk number
                    chunk_sample = d % c;                                       // sample within chunk
                }

                // look-up stco/co64
                if (chunk_id < stbl->max_chunks) {
                    offset = read_xx(&stbl->coxx.data[chunk_id *                // read offset from array
                                                      stbl->coxx.bytes],
                                     stbl->coxx.bytes << 3);
                } else {
                    offset = stbl->max_offset;                                  // use end-of-data
                }

                // look-up stsz
                if (stbl->stsz.size) {
                    offset += chunk_sample * stbl->stsz.size;
                } else if (chunk_sample) {
                    u = chunk_sample;
                    while (u) {
                        offset += read_32(&stbl->stsz.data[(sample_id - u) << 2]);
                        u--;
                    }
                }

                // store offset
                self->offsets[i] = offset;
            }

            // store offsets
            if (self->db) {
                tcadbput(self->db, okey_name, okey_size, self->offsets, sizeof(off_t) * self->periods);
            }
        }

        // normalize limits
        if (self->spatial == true) {
            int i;
            if (self->start) {
                for (i = self->periods - 1; i >= 0; i--) {
                    if (self->offsets[i] < self->start) {
                        self->start = i * self->period;
                        break;
                    }
                }
            }
            if (i < 0) {
                self->start = 0;
            }
            if (self->stop) {
                for (i = self->periods - 1; i >= 0; i--) {
                    if (self->offsets[i] < self->stop) {
                        self->stop = i * self->period;
                        break;
                    }
                }
            }
            if (i < 0) {
                self->stop = 0;
            }
        }

        // reset byte offsets
        self->file_offset = 0;
        self->file_finish = 0;

        // perform seek on each track
        compile_trak(self, &file, &file.moov.vtrak);
        compile_trak(self, &file, &file.moov.strak);

        // recalibrate meta-data
        compile_moov(self, &file);
        compile_mdat(self, &file);

        // asssemble headers/atoms
        compile_head(self, &file);

        // store zero-seek head
        if (self->db && !self->start && !self->stop) {
            off_t limits[2] = { self->file_offset, self->file_finish };
            tcadbput(self->db, hkey_name, hkey_size, self->head, self->head_length);
            tcadbput(self->db, lkey_name, lkey_size, limits, sizeof(off_t) * 2);
        }
    }

    // success
    goto done;

    // error
    error:
    status = 1;

    // done
    done:
    FREE(okey_name);
    FREE(hkey_name);
    FREE(lkey_name);
    FREE(ftyp_key_name);
    FREE(moov_key_name);
    FREE(mdat_key_name);
    FREE(ftyp);
    FREE(moov);
    FREE(mdat);
    return status;
}

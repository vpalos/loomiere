/*
 * The Loomiere Project (http://valeriu.palos.ro/loomiere/).
 *
 * parse_flv.c: FLV parser.
 *
 * Read the LICENSE file!
 * Copyright (C)2010 Valeriu Paloş (valeriu@palos.ro). All rights reserved!
 */

#include <string.h>
#include <unistd.h>

#include "loomiere.h"
#include "stream_flv.h"
#include "stream_flv_meta.h"

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Prepare worker for FLV parsing.
 */
int stream_flv_setup(worker_t* worker) {
    load_stream_flv_meta_lua(worker->lua);
    return 0;
}

/*
 * Parser function implementation for the FLV file format.
 */
int stream_flv_parse(stream_t* self) {

    // exit status
    int status = 0;

    // onMetaData fingerprint
    static char onMetaData[14] = "\x02\x00\x0AonMetaData";

    // search cache
    char* mkey_name = FORMAT("%s:meta", self->path);
    int   mkey_size = strlen(mkey_name);
    char* okey_name = FORMAT("%s:offsets", self->path);
    int   okey_size = strlen(okey_name);
    int   meta_size = 0;
    char* meta_data = NULL;

    // get offsets
    int periods;
    self->offsets = self->db ? tcadbget(self->db, okey_name, okey_size, &periods) : NULL;
    self->periods = periods / sizeof(off_t);

    // avoid zero-seek
    if (self->offsets && !self->start && !self->stop) {

        // count
        (*self->cache_hits)++;

        // prepare
        self->file_offset = 13;
        self->file_finish = self->file_length;

    } else {

        // get cache
        meta_data = self->db ? tcadbget(self->db, mkey_name, mkey_size, &meta_size) : NULL;

        // (re)generate
        if (meta_data) {
            (*self->cache_hits)++;
        } else {
            uint8_t buffer[24];

            if (self->db) {
                (*self->cache_misses)++;
            }

            // check FLV fingerprint
            if (pread(self->file, buffer, 13, 0) != 13 ||
                buffer[0] != 'F' || buffer[1] != 'L' || buffer[2] != 'V' || buffer[3] != 0x01) {
                goto error;
            }

            // find onMetaData
            off_t offset = 13;
            while (1) {

                // read tag
                if (pread(self->file, buffer, 24, offset) != 24) {
                    goto error;
                }
                meta_size = read_24(buffer + 1);
                offset += 11;

                // meta-tags must come before media-tags
                if (buffer[0] != 0x12) {
                    goto error;
                }

                // check onMetaData fingerprint
                int i = sizeof(onMetaData) - 1, found = 1;
                while (found && i-- > 0) {
                    found = onMetaData[i] == buffer[11 + i];
                }

                // read binary
                if (found) {
                    offset += 13;
                    meta_size -= 13;
                    meta_data = (char*)ALLOC(meta_size);
                    if (pread(self->file, meta_data, meta_size, offset) != meta_size) {
                        goto error;
                    } else {
                        break;
                    }
                }

                // find next
                offset += meta_size;
                offset += 4;
            };

            // check failure
            if (!meta_data) {
                goto error;
            }

            // store meta
            if (self->db) {
                tcadbput(self->db, mkey_name, mkey_size, meta_data, meta_size);
            }
        }

        // prepare call
        lua_getglobal(self->lua, "flv");
        lua_getfield(self->lua, -1, "onMetaData");
        lua_remove(self->lua, -2);
        lua_pushlstring(self->lua, meta_data, meta_size);
        lua_pushnumber(self->lua, self->period);
        lua_pushnumber(self->lua, self->start);
        lua_pushnumber(self->lua, self->stop);
        lua_pushboolean(self->lua, self->spatial);
        lua_pushinteger(self->lua, self->file_length);
        FREE(meta_data);

        // invoke compiler
        if (lua_pcall(self->lua, 6, 6, 0) ||
            lua_isnil(self->lua, -1)) {
            lua_pop(self->lua, 1);
            goto error;
        }

        // extract results
        self->stop = lua_tonumber(self->lua, -1);
        self->start = lua_tonumber(self->lua, -2);
        self->file_finish = lua_tointeger(self->lua, -3);
        self->file_offset = lua_tointeger(self->lua, -4);
        self->periods = lua_tointeger(self->lua, -5);
        lua_pop(self->lua, 5);

        // safety
        if (self->start == 0) {
            self->file_offset = 13;
        }
        if (self->stop == 0) {
            self->file_finish = self->file_length;
        }

        // regenerate offsets
        if (!self->offsets) {

            // extract
            int i;
            self->offsets = (off_t*)ZALLOC(self->periods * sizeof(off_t));
            for (i = 0; i < self->periods; i++) {
                lua_rawgeti(self->lua, -1, i + 1);
                self->offsets[i] = lua_tointeger(self->lua, -1);
                lua_pop(self->lua, 1);
            }
            lua_pop(self->lua, 1);

            // store offsets
            if (self->db) {
                tcadbput(self->db, okey_name, okey_size, self->offsets, self->periods * sizeof(off_t));
            }
        }
    }

    // generate HTTP headers
    self->head = FORMAT("HTTP/%s 200 OK\n"
                        "Content-Type: %s\n"
                        "Content-Length: %llu\n"
                        "Cache-Control: no-store, no-cache, must-revalidate, post-check=0, pre-check=0\n"
                        "Expires: Mon, 29 Mar 1982 12:00:00 GMT\n"
                        "Server: %s %s\n\n"
                        ".............",
                        self->http, STREAM_FLV_MIME,
                        (unsigned long long)(self->file_finish - self->file_offset + 13),
                        ID_NAME, ID_VERSION);
    self->head_length = strlen(self->head);
    self->head_offset = 0;

    // inject FLV header
    memcpy(self->head + self->head_length - 13,
           "FLV\x01\x05\x00\x00\x00\x09\x00\x00\x00\x00", 13);

    // success
    goto done;

    // error
    error:
    self->periods = 0;
    FREE(self->offsets);
    status = 1;

    // done
    done:
    FREE(mkey_name);
    FREE(okey_name);
    FREE(meta_data);
    return status;
}

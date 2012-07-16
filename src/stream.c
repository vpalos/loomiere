/*
 * The Loomiere Project (http://valeriu.palos.ro/loomiere/).
 *
 * stream.h: A self-manageable stream object.
 *
 * Read the LICENSE file!
 * Copyright (C)2010 Valeriu Paloş (valeriu@palos.ro). All rights reserved!
 */

#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "core.h"
#include "loomiere.h"
#include "stream.h"
#include "stream_flv.h"
#include "stream_mp4.h"

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Cross-platform abstractions for socket system calls. All wrappers
 * must have identical prototypes. All wrappers that return int, will
 * return 0 to signal success!
 */

/*
 * ssize_t _sendfile(int socket, int file, off_t offset, size_t bytes)
 */
#if defined(__linux__)
    #include <sys/sendfile.h>
    ssize_t _sendfile(int socket, int file, off_t offset, size_t bytes) {
        return sendfile(socket, file, &offset, bytes);
    }
#elif defined(__FreeBSD__)
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <sys/uio.h>
    ssize_t _sendfile(int socket, int file, off_t offset, size_t bytes) {
        off_t result;
        if (sendfile(file, socket, offset, bytes, NULL, &result, 0)) {
            return -1;
        }
        return result;
    }
#else
    #error "The _sendfile() wrapper is not (yet) implemented on this system! Aborting..."
#endif

/*
 * int _setcork(int socket, int enable)
 */
#if defined(__linux__)
    #include <fcntl.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    int _setcork(int socket, int enable) {
        setsockopt(socket, IPPROTO_TCP, TCP_CORK, &enable, sizeof(enable));
        int flags = fcntl(socket, F_GETFL);
        return (flags == -1 || fcntl(socket, F_SETFL, flags | O_NONBLOCK));
    }
#else
    #warning "The _setcork() wrapper is not (yet) implemented on this system! Ignoring..."
    int _setcork(int socket, int enable) {
        return 0;
    }
#endif

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Watcher prototypes.
 */
static void _hint_cb(struct ev_loop*, ev_io*, int);
static void _send_cb(struct ev_loop*, ev_io*, int);
static void _wait_cb(struct ev_loop*, ev_timer*, int);
static void _jump_cb(struct ev_loop*, ev_timer*, int);

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Schedule the sending process.
 */
static void _stream_schedule(stream_t* self) {

    // stop workers
    ev_io_stop(self->loop, &self->send_w);
    ev_timer_stop(self->loop, &self->wait_w);

    // schedule jump
    ev_timer_set(&self->jump_w, self->period, 0);
    ev_timer_start(self->loop, &self->jump_w);
}

/*
 * Start the sending process.
 */
static void _stream_advance(stream_t* self) {

    // start workers
    _wait_cb(self->loop, &self->wait_w, EV_TIMEOUT);
    ev_io_start(self->loop, &self->send_w);
}

/*
 * Generic (fake) parser to allow sending any file.
 */
int _stream_any_parse(stream_t* self) {

    // headers
    self->head = FORMAT("HTTP/%s 200 OK\n"
                        "Content-Type: %s\n"
                        "Content-Length: %llu\n"
                        "Cache-Control: no-store, no-cache, must-revalidate, post-check=0, pre-check=0\n"
                        "Expires: Mon, 29 Mar 1982 12:00:00 GMT\n"
                        "Server: %s %s\n\n",
                        self->http, self->mime, (unsigned long long)self->file_length,
                        ID_NAME, ID_VERSION);
    self->head_length = strlen(self->head);
    self->head_offset = 0;

    // options
    self->throttle = 0;
    self->file_offset = 0;
    self->file_target = self->file_finish = self->file_length;

    return 0;
}

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Read and process hint.
 */
static void _hint_cb(struct ev_loop* loop, ev_io* watcher, int events) {

    // initialize
    stream_t* self = (stream_t*)(((char*)watcher) - offsetof(stream_t, hint_w));

    // TODO: implement (AMF) hint capture!
}

/*
 * Retry advancing.
 */
static void _jump_cb(struct ev_loop* loop, ev_timer* watcher, int events) {

    // initialize
    stream_t* self = (stream_t*)(((char*)watcher) - offsetof(stream_t, jump_w));

    // advance
    _stream_advance(self);
}

/*
 * Push data onto the socket.
 */
static void _send_cb(struct ev_loop* loop, ev_io* watcher, int events) {

    // initialize
    stream_t* self = (stream_t*)(((char*)watcher) - offsetof(stream_t, send_w));
    ssize_t result;

    // activity
    ev_tstamp now = ev_now(loop);
    self->last_send = now;

    // send headers
    if (self->head) {

        // push data
        result = 0;
        if (self->head_length - self->head_offset) {
            result = write(self->socket,
                           self->head + self->head_offset,
                           self->head_length - self->head_offset);
            if (result == -1) {
                result = 0;
                if (errno != EAGAIN && errno != EINTR) {
                    goto finish;
                }
            }
        }

        // advance/retry
        self->head_offset += result;
        (*self->data_total) += result;
        if (self->head_offset < self->head_length) {
            return;
        }

        // complete
        self->head_offset = self->head_length = 0;
        FREE(self->head);
    }

    // pivot load delay
    off_t old_file_target = self->file_target;
    ev_tstamp old_load_head = self->load_head;

    // (re)throttle
    if (self->throttle) {

        // measure
        ev_tstamp play_head = now - self->tzero;
        self->load_head = self->start + play_head + self->throttle;
        off_t target = (off_t)ceil(self->load_head / self->period);
        if (target >= self->periods) {
            self->file_target = self->file_finish;
        } else {
            self->file_target = self->offsets[target];
        }
    } else {
        self->file_target = self->file_finish;
    }

    // cumulate load delay
    double  dd = 0;
    double* ds = self->delay_sum;
    double* dc = self->delay_count;
    double* da = self->delay_average;

    // overflow
    if ((*dc) >= 1000000000) {
        (*ds) = 0;
        (*dc) = 0;
    }

    // measure
    if (self->file_offset < old_file_target) {
        dd = self->load_head - old_load_head;
    }
    // cumulate
    (*ds) += dd;
    (*dc)++;
    (*da) = (*ds) / (*dc);

    // push file data
    result = 0;
    if (self->file_target - self->file_offset) {
        result = _sendfile(self->socket, self->file,
                           self->file_offset,
                           self->file_target - self->file_offset);
        if (result == -1) {
            result = 0;
            if (errno != EAGAIN && errno != EINTR) {
                goto finish;
            }
        }
    }

    // advance/retry
    self->file_offset += result;
    (*self->data_total) += result;
    if (self->file_offset < self->file_target) {
        return;
    }

    // complete/finish
    if (self->file_offset == self->file_finish) {
        goto finish;
    }

    // pop cork on first full target
    if (self->nagle) {
        self->nagle = 0;
        if (_setcork(self->socket, self->nagle)) {
            goto finish;
        }
    }

    // re(schedule)
    _stream_schedule(self);
    return;

    // destroy
    finish:
    stream_destroy(self);
    FREE(self);
}

/*
 * Timeout catcher.
 */
static void _wait_cb(struct ev_loop* loop, ev_timer* watcher, int events) {

    // initialize
    stream_t* self = (stream_t*)(((char*)watcher) - offsetof(stream_t, wait_w));

    // measure
    ev_tstamp now = ev_now(loop);
    ev_tstamp out = self->last_send + STREAM_THROTTLE_TIMEOUT;

    // perform
    if (out < now) {
        stream_destroy(self);
        FREE(self);
    } else {
        watcher->repeat = out - now;
        ev_timer_again(loop, watcher);
    }
}

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Send an error HTTP response and destroy stream.
 */
static void _stream_error(stream_t* self, const char* code) {

    // log error
    WARNING("File \"%s\" could not be served (%s)!", self->path, code);

    // cancel
    self->throttle = 0;
    self->file_offset = self->file_target = self->file_finish = 0;

    // headers
    self->head = FORMAT("HTTP/%s %s\n",
                        self->http, code, ID_NAME, ID_VERSION);
    self->head_length = strlen(self->head);

    // trigger transfer
    self->last_send = ev_now(self->loop);
    _stream_advance(self);
}

/*----------------------------------------------------------------------------------------------------------*/

/*
 * Constructor (arguments must be prepared in self). If successful,
 * this function will entirely take over the created stream. In case
 * of failure (only) the caller must call stream_destroy() and FREE()
 * on the stream opbject to ensure proper clean-up.
 */
int stream_new(stream_t* self) {

    // increase load
    (*self->load)++;

    // initialize watchers
    ev_io_init(&self->hint_w, _hint_cb, self->socket, EV_READ);
    ev_io_init(&self->send_w, _send_cb, self->socket, EV_WRITE);
    ev_init(&self->jump_w, _jump_cb);
    ev_init(&self->wait_w, _wait_cb);

    // choose parser
    stream_f parse = NULL;
    if (!strcmp(self->mime, STREAM_MP4_MIME)) {
        parse = stream_mp4_parse;
    } else if (!strcmp(self->mime, STREAM_FLV_MIME)) {
        parse = stream_flv_parse;
    } else {
        parse = _stream_any_parse;
        self->throttle = 0;
    }

    // initialize
    self->file = open(self->path, O_RDONLY);
    if (self->file < 0) goto error;

    // get length
    self->file_length = lseek(self->file, 0, SEEK_END);
    if (self->file_length < 0) goto error;

    // parse
    if (parse(self)) goto error;

    // push cork
    self->nagle = 1;
    _setcork(self->socket, self->nagle);

    // cancel throttling for small chunks
    if ((self->file_finish - self->file_offset) <= STREAM_THROTTLE_FROM) {
        self->throttle = 0;
    }

    // trigger transfer
    self->last_send = self->tzero = ev_now(self->loop);
    _stream_advance(self);

    // success
    goto done;

    // error
    error:
    _stream_error(self, "500 Internal Server Error");

    // done
    done:
    return 0;
}

/*
 * Destructor.
 */
int stream_destroy(stream_t* self) {

    // decrease load
    (*self->load)--;

    // pop cork
    self->nagle = 0;
    _setcork(self->socket, self->nagle);

    // stop watchers
    ev_io_stop(self->loop, &self->hint_w);
    ev_io_stop(self->loop, &self->send_w);
    ev_timer_stop(self->loop, &self->jump_w);
    ev_timer_stop(self->loop, &self->wait_w);

    // close socket
    if (self->socket) {
        close(self->socket);
    }

    // close file
    if (self->file) {
        close(self->file);
    }

    // purge members
    FREE(self->path);
    FREE(self->mime);
    FREE(self->hint);
    FREE(self->head);
    FREE(self->offsets);
    ZERO(self, sizeof(stream_t));

    // done
    return 0;
}

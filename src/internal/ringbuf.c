#include "ringbuf.h"
#include <stdlib.h>
#include <string.h>

int ws_ringbuf_init(ws_ringbuf_t* rb, size_t capacity) {
    memset(rb, 0, sizeof(*rb));
    rb->buffer = (uint8_t*)malloc(capacity);
    if (!rb->buffer) return -1;
    rb->capacity = capacity;
    rb->head = rb->tail = 0;
    rb->count = 0;
    return 0;
}

void ws_ringbuf_free(ws_ringbuf_t* rb) {
    if (rb && rb->buffer) free(rb->buffer);
    if (rb) memset(rb, 0, sizeof(*rb));
}

static size_t advance_index(size_t idx, size_t n, size_t cap) {
    idx += n; if (idx >= cap) idx -= cap; return idx;
}

size_t ws_ringbuf_size(const ws_ringbuf_t* rb) { return rb->count; }

size_t ws_ringbuf_available(const ws_ringbuf_t* rb) { return rb->capacity - rb->count; }

bool ws_ringbuf_is_empty(const ws_ringbuf_t* rb) { return rb->count == 0; }

bool ws_ringbuf_is_full(const ws_ringbuf_t* rb) { return rb->count == rb->capacity; }

size_t ws_ringbuf_peek_read(const ws_ringbuf_t* rb, const uint8_t** out_ptr) {
    size_t readable = ws_ringbuf_size(rb);
    if (readable == 0) { *out_ptr = NULL; return 0; }
    size_t tail_to_end;
    if (rb->tail < rb->head) {
        /* contiguous region from tail up to head */
        tail_to_end = rb->head - rb->tail;
    } else {
        /* wrapped: read until end */
        tail_to_end = rb->capacity - rb->tail;
    }
    if (tail_to_end > readable) tail_to_end = readable;
    *out_ptr = rb->buffer + rb->tail;
    return tail_to_end;
}

void ws_ringbuf_consume(ws_ringbuf_t* rb, size_t nbytes) {
    if (nbytes == 0 || rb->capacity == 0) { return; }
    size_t readable = rb->count;
    if (nbytes > readable) nbytes = readable;
    rb->tail = advance_index(rb->tail, nbytes, rb->capacity);
    rb->count -= nbytes;
}

size_t ws_ringbuf_peek_write(const ws_ringbuf_t* rb, uint8_t** out_ptr) {
    if (rb->count == rb->capacity) { *out_ptr = NULL; return 0; }
    size_t avail = ws_ringbuf_available(rb);
    size_t head_to_end;
    if (rb->head < rb->tail) {
        /* contiguous free region before tail */
        head_to_end = rb->tail - rb->head;
    } else {
        /* space to end */
        head_to_end = rb->capacity - rb->head;
    }
    if (head_to_end > avail) head_to_end = avail;
    *out_ptr = rb->buffer + rb->head;
    return head_to_end;
}

void ws_ringbuf_commit(ws_ringbuf_t* rb, size_t nbytes) {
    if (nbytes == 0 || rb->capacity == 0) return;
    size_t space = rb->capacity - rb->count;
    if (nbytes > space) nbytes = space;
    rb->head = advance_index(rb->head, nbytes, rb->capacity);
    rb->count += nbytes;
}

size_t ws_ringbuf_write_copy(ws_ringbuf_t* rb, const uint8_t* data, size_t len) {
    size_t written = 0;
    while (len) {
        uint8_t* wptr; size_t space = ws_ringbuf_peek_write(rb, &wptr);
        if (space == 0) break;
        size_t n = (len < space) ? len : space;
        memcpy(wptr, data, n);
        ws_ringbuf_commit(rb, n);
        data += n; len -= n; written += n;
    }
    return written;
}

size_t ws_ringbuf_read_copy(ws_ringbuf_t* rb, uint8_t* out, size_t len) {
    size_t read = 0;
    while (len) {
        const uint8_t* rptr; size_t have = ws_ringbuf_peek_read(rb, &rptr);
        if (have == 0) break;
        size_t n = (len < have) ? len : have;
        memcpy(out, rptr, n);
        ws_ringbuf_consume(rb, n);
        out += n; len -= n; read += n;
    }
    return read;
}



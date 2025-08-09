#ifndef WIBESOCKET_INTERNAL_RINGBUF_H
#define WIBESOCKET_INTERNAL_RINGBUF_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t* buffer;
    size_t   capacity;
    size_t   head;  /* write index */
    size_t   tail;  /* read index */
    size_t   count; /* bytes stored */
} ws_ringbuf_t;

int  ws_ringbuf_init(ws_ringbuf_t* rb, size_t capacity);
void ws_ringbuf_free(ws_ringbuf_t* rb);

size_t ws_ringbuf_size(const ws_ringbuf_t* rb);
size_t ws_ringbuf_available(const ws_ringbuf_t* rb);
bool   ws_ringbuf_is_empty(const ws_ringbuf_t* rb);
bool   ws_ringbuf_is_full(const ws_ringbuf_t* rb);

/* Zero-copy read: returns contiguous readable region */
size_t ws_ringbuf_peek_read(const ws_ringbuf_t* rb, const uint8_t** out_ptr);
void   ws_ringbuf_consume(ws_ringbuf_t* rb, size_t nbytes);

/* Zero-copy write: returns contiguous writable region */
size_t ws_ringbuf_peek_write(const ws_ringbuf_t* rb, uint8_t** out_ptr);
void   ws_ringbuf_commit(ws_ringbuf_t* rb, size_t nbytes);

/* Fallback convenience copy API */
size_t ws_ringbuf_write_copy(ws_ringbuf_t* rb, const uint8_t* data, size_t len);
size_t ws_ringbuf_read_copy(ws_ringbuf_t* rb, uint8_t* out, size_t len);

#endif /* WIBESOCKET_INTERNAL_RINGBUF_H */



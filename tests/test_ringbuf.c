#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../src/internal/ringbuf.h"

static void test_basic_rw(void) {
    ws_ringbuf_t rb; assert(ws_ringbuf_init(&rb, 16) == 0);
    printf("[rb] init16 ok\n");
    assert(ws_ringbuf_is_empty(&rb));
    uint8_t data[10]; for (int i = 0; i < 10; i++) data[i] = (uint8_t)i;
    size_t w = ws_ringbuf_write_copy(&rb, data, 10);
    printf("[rb] wrote %zu\n", w);
    assert(w == 10);
    assert(ws_ringbuf_size(&rb) == 10);
    uint8_t out[10]; size_t r = ws_ringbuf_read_copy(&rb, out, 10);
    printf("[rb] read %zu\n", r);
    assert(r == 10);
    assert(memcmp(out, data, 10) == 0);
    assert(ws_ringbuf_is_empty(&rb));
    ws_ringbuf_free(&rb);
}

static void test_wrap_and_zero_copy(void) {
    ws_ringbuf_t rb; assert(ws_ringbuf_init(&rb, 8) == 0);
    printf("[rb] init8 ok\n");
    uint8_t a[6]; memset(a, 'A', sizeof(a));
    uint8_t b[6]; memset(b, 'B', sizeof(b));
    size_t w = ws_ringbuf_write_copy(&rb, a, 6);
    printf("[rb] wrote A = %zu size=%zu avail=%zu\n", w, ws_ringbuf_size(&rb), ws_ringbuf_available(&rb));
    assert(w == 6);
    printf("[rb] wrote 6 A\n");
    const uint8_t* rptr; size_t have = ws_ringbuf_peek_read(&rb, &rptr);
    assert(have == 6);
    ws_ringbuf_consume(&rb, 6);
    printf("[rb] consumed 6\n");
    assert(ws_ringbuf_write_copy(&rb, b, 6) == 6);
    printf("[rb] wrote 6 B\n");
    uint8_t out[6]; size_t r = ws_ringbuf_read_copy(&rb, out, 6);
    printf("[rb] read B = %zu size=%zu avail=%zu\n", r, ws_ringbuf_size(&rb), ws_ringbuf_available(&rb));
    assert(r == 6);
    assert(memcmp(out, b, 6) == 0);
    ws_ringbuf_free(&rb);
}

int main(void) {
    test_basic_rw();
    test_wrap_and_zero_copy();
    printf("test_ringbuf OK\n");
    return 0;
}



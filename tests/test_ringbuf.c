#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../src/internal/ringbuf.h"

static void test_basic_rw(void) {
    ws_ringbuf_t rb; assert(ws_ringbuf_init(&rb, 16) == 0);
    assert(ws_ringbuf_is_empty(&rb));
    uint8_t data[10]; for (int i = 0; i < 10; i++) data[i] = (uint8_t)i;
    size_t w = ws_ringbuf_write_copy(&rb, data, 10);
    assert(w == 10);
    assert(ws_ringbuf_size(&rb) == 10);
    uint8_t out[10]; size_t r = ws_ringbuf_read_copy(&rb, out, 10);
    assert(r == 10);
    assert(memcmp(out, data, 10) == 0);
    assert(ws_ringbuf_is_empty(&rb));
    ws_ringbuf_free(&rb);
}

static void test_wrap_and_zero_copy(void) {
    ws_ringbuf_t rb; assert(ws_ringbuf_init(&rb, 8) == 0);
    uint8_t a[6]; memset(a, 'A', sizeof(a));
    uint8_t b[6]; memset(b, 'B', sizeof(b));
    assert(ws_ringbuf_write_copy(&rb, a, 6) == 6);
    const uint8_t* rptr; size_t have = ws_ringbuf_peek_read(&rb, &rptr);
    assert(have > 0);
    ws_ringbuf_consume(&rb, have);
    assert(ws_ringbuf_write_copy(&rb, b, 6) == 6);
    uint8_t out[6]; assert(ws_ringbuf_read_copy(&rb, out, 6) == 6);
    assert(memcmp(out, b, 6) == 0);
    ws_ringbuf_free(&rb);
}

int main(void) {
    test_basic_rw();
    test_wrap_and_zero_copy();
    printf("test_ringbuf OK\n");
    return 0;
}



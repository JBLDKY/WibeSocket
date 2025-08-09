#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "wibesocket/wibesocket.h"
#include "../src/internal/frame.h"

static size_t make_frame(uint8_t* out, size_t cap, int fin, uint8_t opcode,
                         int masked, const uint8_t mask[4],
                         const uint8_t* payload, size_t len) {
    size_t need = 2;
    if (len <= 125) need += 0; else if (len <= 0xFFFF) need += 2; else need += 8;
    if (masked) need += 4;
    need += len;
    if (need > cap) return 0;

    out[0] = (uint8_t)((fin ? 0x80 : 0x00) | (opcode & 0x0F));
    if (len <= 125) {
        out[1] = (uint8_t)len;
    } else if (len <= 0xFFFF) {
        out[1] = 126;
        out[2] = (uint8_t)((len >> 8) & 0xFF);
        out[3] = (uint8_t)(len & 0xFF);
    } else {
        out[1] = 127;
        for (int i = 0; i < 8; i++) out[2 + i] = (uint8_t)((len >> (56 - i * 8)) & 0xFF);
    }
    if (masked) out[1] |= 0x80;
    size_t pos = 2;
    if (out[1] == 126) pos = 4; else if (out[1] == 127) pos = 10;
    if (masked) {
        memcpy(out + pos, mask, 4); pos += 4;
    }
    if (payload && len) memcpy(out + pos, payload, len);
    return need;
}

static void test_short_payload_unmasked(void) {
    ws_parser_t p; ws_parser_init(&p, 1 << 20);
    uint8_t buf[64];
    const uint8_t payload[] = {1,2,3};
    size_t n = make_frame(buf, sizeof(buf), 1, 0x2, 0, NULL, payload, sizeof(payload));
    assert(n > 0);
    size_t consumed = 0; ws_parsed_frame_t f;
    ws_parser_status_t s = ws_parser_feed(&p, buf, n, &consumed, &f);
    assert(s == WS_PARSER_FRAME);
    assert(consumed == n);
    assert(f.type == WIBESOCKET_FRAME_BINARY);
    assert(f.is_final == true);
    assert(f.payload_len == 3);
}

static void test_extended_16_unmasked(void) {
    ws_parser_t p; ws_parser_init(&p, 1 << 20);
    uint8_t buf[256];
    uint8_t payload[200]; memset(payload, 0xAB, sizeof(payload));
    size_t n = make_frame(buf, sizeof(buf), 1, 0x2, 0, NULL, payload, sizeof(payload));
    assert(n > 0);
    size_t c = 0; ws_parsed_frame_t f; ws_parser_status_t s = ws_parser_feed(&p, buf, n, &c, &f);
    assert(s == WS_PARSER_FRAME);
    assert(c == n);
    assert(f.type == WIBESOCKET_FRAME_BINARY && f.is_final);
    assert(f.payload_len == sizeof(payload));
}

static void test_control_frame_rules(void) {
    ws_parser_t p; ws_parser_init(&p, 1 << 20);
    uint8_t buf[64]; uint8_t m[4] = {0,0,0,0};
    (void)m;
    /* control frame fragmented -> error */
    size_t n = make_frame(buf, sizeof(buf), 0, 0x9, 0, NULL, NULL, 0);
    assert(n > 0);
    size_t c = 0; ws_parsed_frame_t f; ws_parser_status_t s = ws_parser_feed(&p, buf, n, &c, &f);
    assert(s == WS_PARSER_ERROR_PROTOCOL);
    /* control frame >125 -> error */
    ws_parser_init(&p, 1 << 20);
    uint8_t big[200]; memset(big, 0, sizeof(big));
    uint8_t buf_big[512];
    n = make_frame(buf_big, sizeof(buf_big), 1, 0x9, 0, NULL, big, 126);
    assert(n > 0);
    c = 0; s = ws_parser_feed(&p, buf_big, n, &c, &f);
    assert(s == WS_PARSER_ERROR_PROTOCOL);
}

int main(void) {
    test_short_payload_unmasked();
    test_extended_16_unmasked();
    test_control_frame_rules();
    printf("test_parser OK\n");
    return 0;
}

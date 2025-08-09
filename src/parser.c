/* Incremental WebSocket frame parser per RFC 6455 (no extensions) */
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "internal/frame.h"
#include "internal/utf8.h"

static void ws_apply_mask(uint8_t* dst, const uint8_t* src, size_t len, const uint8_t mask[4]) {
    for (size_t i = 0; i < len; i++) dst[i] = (uint8_t)(src[i] ^ mask[i & 3]);
}

static bool ws_is_valid_close_code(uint16_t code) {
    if (code == 1000 || code == 1001 || code == 1002 || code == 1003 ||
        code == 1007 || code == 1008 || code == 1009 || code == 1010 ||
        code == 1011) return true;
    /* 1004 reserved; 1005/1006 not to be sent; 1015 TLS; 0-999 invalid; 1016-2999 reserved */
    return false;
}

void ws_parser_init(ws_parser_t* p, uint64_t max_frame_size) {
    memset(p, 0, sizeof(*p));
    p->max_frame_size = max_frame_size ? max_frame_size : (1ULL << 20);
    p->hdr_need = 2; /* first 2 header bytes */
}

static int ws_parse_header(ws_parser_t* p) {
    if (p->hdr_have < p->hdr_need) return 0; /* need more */
    const uint8_t* h = p->hdr_bytes;
    uint8_t b0 = h[0];
    uint8_t b1 = h[1];
    p->cur.fin = (b0 & 0x80U) != 0;
    p->cur.rsv = (uint8_t)((b0 >> 4) & 0x07U);
    p->cur.opcode = (ws_opcode_t)(b0 & 0x0FU);
    p->cur.masked = (b1 & 0x80U) != 0;
    uint64_t plen7 = (uint64_t)(b1 & 0x7FU);

    if (p->cur.rsv != 0) return WS_PARSER_ERROR_PROTOCOL;
    if (p->cur.opcode >= 0x3 && p->cur.opcode <= 0x7) return WS_PARSER_ERROR_PROTOCOL; /* reserved */
    if (p->cur.opcode >= 0xB) return WS_PARSER_ERROR_PROTOCOL; /* reserved */

    size_t need = 2;
    if (plen7 <= 125) {
        p->cur.payload_len = plen7;
    } else if (plen7 == 126) {
        need += 2;
        if (p->hdr_have < need) { p->hdr_need = need; return 0; }
        p->cur.payload_len = ((uint64_t)h[2] << 8) | (uint64_t)h[3];
    } else { /* 127 */
        need += 8;
        if (p->hdr_have < need) { p->hdr_need = need; return 0; }
        uint64_t v = 0;
        for (int i = 0; i < 8; i++) v = (v << 8) | h[2 + i];
        p->cur.payload_len = v;
        /* Disallow lengths with MSB set per RFC (must not be negative when interpreted as signed) */
        if (h[2] & 0x80U) return WS_PARSER_ERROR_PROTOCOL;
    }

    size_t after_len = (size_t)(need);
    if (p->cur.masked) {
        need += 4;
        if (p->hdr_have < need) { p->hdr_need = need; return 0; }
        memcpy(p->cur.mask_key, h + after_len, 4);
    }

    /* Control frame rules */
    bool is_control = (p->cur.opcode & 0x08U) != 0;
    if (is_control) {
        if (!p->cur.fin) return WS_PARSER_ERROR_PROTOCOL; /* control frames must not be fragmented */
        if (p->cur.payload_len > 125) return WS_PARSER_ERROR_PROTOCOL;
    }

    if (p->cur.payload_len > p->max_frame_size) return WS_PARSER_ERROR_TOO_LARGE;

    return 1; /* header complete */
}

ws_parser_status_t ws_parser_feed(ws_parser_t* p,
                                  const uint8_t* data, size_t len,
                                  size_t* consumed,
                                  ws_parsed_frame_t* out_frame) {
    *consumed = 0;
    p->out_payload = NULL;
    p->out_payload_len = 0;

    /* Accumulate header first; ws_parse_header may request more bytes progressively */
    for (;;) {
        while (p->hdr_have < p->hdr_need && *consumed < len) {
            p->hdr_bytes[p->hdr_have++] = data[*consumed];
            (*consumed)++;
        }
        int hdr_status = ws_parse_header(p);
        if (hdr_status < 0) return (ws_parser_status_t)hdr_status;
        if (hdr_status == 0) {
            if (*consumed == len) return WS_PARSER_NEED_MORE;
            /* Otherwise loop to pull more header bytes now that hdr_need may have increased */
            continue;
        }
        /* Header complete */
        break;
    }

    /* Header complete; now expect payload_len bytes */
    uint64_t need = p->cur.payload_len - p->payload_read;
    size_t avail = len - *consumed;
    size_t take = (size_t)((need < (uint64_t)avail) ? need : (uint64_t)avail);
    const uint8_t* payload_start = data + *consumed;
    *consumed += take;
    p->payload_read += take;

    /* Expose zero-copy payload view of the chunk we just consumed */
    p->out_payload = payload_start;
    p->out_payload_len = take;

    if (p->payload_read < p->cur.payload_len) {
        /* Need more data for this frame */
        if (take == 0) return WS_PARSER_NEED_MORE;
        /* Provide partial data frames (caller can accumulate); no frame signal yet */
        return WS_PARSER_NEED_MORE;
    }

    /* Entire payload read; construct frame */
    ws_parsed_frame_t f;
    f.type = (ws_opcode_t)p->cur.opcode;
    f.payload = p->out_payload;
    f.payload_len = (size_t)p->out_payload_len; /* could be less than total if spread across feeds */
    f.is_final = p->cur.fin;

    /* Fragmentation tracking */
    bool is_control = ((p->cur.opcode & 0x08U) != 0);
    if (!is_control) {
        if (p->cur.opcode == WS_OPCODE_CONTINUATION) {
            if (!p->in_fragmented_message) return WS_PARSER_ERROR_PROTOCOL;
            if (p->cur.fin) p->in_fragmented_message = false;
        } else { /* data opcode */
            if (p->in_fragmented_message) return WS_PARSER_ERROR_PROTOCOL; /* new data while mid-frag */
            if (!p->cur.fin) {
                p->in_fragmented_message = true;
                p->first_fragment_opcode = p->cur.opcode;
            }
        }
    }

    /* Validate UTF-8 for text frames and close reason if present */
    if ((p->cur.opcode == WS_OPCODE_TEXT || (p->cur.opcode == WS_OPCODE_CONTINUATION && p->first_fragment_opcode == WS_OPCODE_TEXT)) &&
        f.payload_len > 0) {
        if (!ws_utf8_is_valid((const uint8_t*)f.payload, f.payload_len)) {
            return WS_PARSER_ERROR_PROTOCOL;
        }
    }

    if (p->cur.opcode == WS_OPCODE_CLOSE) {
        /* Payload must be 0 or >= 2. If >=2, first 2 bytes are code, rest is UTF-8 reason */
        if (p->cur.payload_len == 1) return WS_PARSER_ERROR_PROTOCOL;
        if (p->cur.payload_len >= 2) {
            const uint8_t* b = (const uint8_t*)f.payload;
            uint16_t code = ((uint16_t)b[0] << 8) | (uint16_t)b[1];
            if (!ws_is_valid_close_code(code)) return WS_PARSER_ERROR_PROTOCOL;
            size_t rlen = f.payload_len - 2;
            if (rlen > 0 && !ws_utf8_is_valid(b + 2, rlen)) return WS_PARSER_ERROR_PROTOCOL;
        }
    }

    /* Reset for next frame */
    p->hdr_need = 2;
    p->hdr_have = 0;
    p->payload_read = 0;

    if (out_frame) *out_frame = f;
    return WS_PARSER_FRAME;
}

size_t ws_build_frame(uint8_t* out, size_t out_cap,
                      int fin, ws_opcode_t opcode,
                      const uint8_t mask_key[4],
                      const uint8_t* payload, size_t payload_len) {
    size_t need = 2;
    if (payload_len <= 125) need += 0; else if (payload_len <= 0xFFFF) need += 2; else need += 8;
    if (mask_key) need += 4;
    need += payload_len;
    if (need > out_cap) return 0;

    out[0] = (uint8_t)((fin ? 0x80 : 0) | (opcode & 0x0F));
    size_t pos = 2;
    if (payload_len <= 125) {
        out[1] = (uint8_t)payload_len;
    } else if (payload_len <= 0xFFFF) {
        out[1] = 126;
        out[2] = (uint8_t)((payload_len >> 8) & 0xFF);
        out[3] = (uint8_t)(payload_len & 0xFF);
        pos = 4;
    } else {
        out[1] = 127;
        for (int i = 0; i < 8; i++) out[2 + i] = (uint8_t)((payload_len >> (56 - 8 * i)) & 0xFF);
        pos = 10;
    }
    if (mask_key) {
        out[1] |= 0x80;
        memcpy(out + pos, mask_key, 4);
        pos += 4;
    }
    if (payload_len) {
        if (mask_key) {
            for (size_t i = 0; i < payload_len; i++) out[pos + i] = (uint8_t)(payload[i] ^ mask_key[i & 3]);
        } else if (payload) {
            memcpy(out + pos, payload, payload_len);
        }
    }
    return need;
}


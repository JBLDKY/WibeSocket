#ifndef WIBESOCKET_INTERNAL_FRAME_H
#define WIBESOCKET_INTERNAL_FRAME_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    WS_OPCODE_CONTINUATION = 0x0,
    WS_OPCODE_TEXT         = 0x1,
    WS_OPCODE_BINARY       = 0x2,
    WS_OPCODE_CLOSE        = 0x8,
    WS_OPCODE_PING         = 0x9,
    WS_OPCODE_PONG         = 0xA,
} ws_opcode_t;

typedef enum {
    WS_PARSER_OK = 0,
    WS_PARSER_NEED_MORE = 1,
    WS_PARSER_FRAME = 2,
    WS_PARSER_ERROR_PROTOCOL = -1,
    WS_PARSER_ERROR_TOO_LARGE = -2,
} ws_parser_status_t;

typedef struct {
    bool     fin;
    uint8_t  rsv;
    ws_opcode_t opcode;
    bool     masked;
    uint64_t payload_len;
    uint8_t  mask_key[4];
} ws_frame_header_t;

typedef struct {
    /* Config */
    uint64_t max_frame_size;

    /* Incremental state */
    uint8_t  hdr_bytes[2 + 8 + 4];
    size_t   hdr_need;   /* number of header bytes needed next */
    size_t   hdr_have;   /* number of header bytes already read */
    ws_frame_header_t cur;
    uint64_t payload_read; /* bytes of payload read so far */

    /* Message fragmentation tracking */
    bool     in_fragmented_message;
    ws_opcode_t first_fragment_opcode;

    /* Output view (zero-copy pointer into the last fed chunk) */
    const uint8_t* out_payload;
    size_t         out_payload_len;
} ws_parser_t;

typedef struct {
    ws_opcode_t type;
    const void* payload;
    size_t      payload_len;
    bool        is_final;
} ws_parsed_frame_t;

void ws_parser_init(ws_parser_t* p, uint64_t max_frame_size);

/* Feed a chunk. On WS_PARSER_FRAME, fills out_frame and sets consumed to the number of bytes
 * consumed from input. The out_payload points into the input buffer.
 */
ws_parser_status_t ws_parser_feed(ws_parser_t* p,
                                  const uint8_t* data, size_t len,
                                  size_t* consumed,
                                  ws_parsed_frame_t* out_frame);

/* Build a single WebSocket frame into out buffer. Returns number of bytes written or 0 on error.
 * If mask_key is non-NULL, client masking is applied; otherwise unmasked.
 */
size_t ws_build_frame(uint8_t* out, size_t out_cap,
                      int fin, ws_opcode_t opcode,
                      const uint8_t mask_key[4],
                      const uint8_t* payload, size_t payload_len);

#endif /* WIBESOCKET_INTERNAL_FRAME_H */



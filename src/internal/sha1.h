#ifndef WIBESOCKET_INTERNAL_SHA1_H
#define WIBESOCKET_INTERNAL_SHA1_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t state[5];
    uint64_t count; /* bits processed */
    uint8_t  buffer[64];
} ws_sha1_ctx_t;

void ws_sha1_init(ws_sha1_ctx_t* ctx);
void ws_sha1_update(ws_sha1_ctx_t* ctx, const void* data, size_t len);
void ws_sha1_final(ws_sha1_ctx_t* ctx, uint8_t out_digest[20]);

#endif /* WIBESOCKET_INTERNAL_SHA1_H */



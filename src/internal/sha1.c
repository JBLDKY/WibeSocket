#include "sha1.h"
#include <string.h>

/* Minimal SHA-1 (public domain style) */

#define ROTL32(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void ws_sha1_transform(uint32_t state[5], const uint8_t block[64]) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) |
               ((uint32_t)block[i * 4 + 3]);
    }
    for (int i = 16; i < 80; i++) {
        w[i] = ROTL32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];

    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999U;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1U;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCU;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6U;
        }
        uint32_t temp = ROTL32(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = ROTL32(b, 30);
        b = a;
        a = temp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

void ws_sha1_init(ws_sha1_ctx_t* ctx) {
    ctx->state[0] = 0x67452301U;
    ctx->state[1] = 0xEFCDAB89U;
    ctx->state[2] = 0x98BADCFEU;
    ctx->state[3] = 0x10325476U;
    ctx->state[4] = 0xC3D2E1F0U;
    ctx->count = 0;
    memset(ctx->buffer, 0, sizeof(ctx->buffer));
}

void ws_sha1_update(ws_sha1_ctx_t* ctx, const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    size_t idx = (size_t)((ctx->count >> 3) & 63U);
    ctx->count += ((uint64_t)len) << 3;

    size_t space = 64U - idx;
    if (len >= space) {
        memcpy(ctx->buffer + idx, p, space);
        ws_sha1_transform(ctx->state, ctx->buffer);
        p += space;
        len -= space;
        while (len >= 64U) {
            ws_sha1_transform(ctx->state, p);
            p += 64U;
            len -= 64U;
        }
        idx = 0;
    }
    memcpy(ctx->buffer + idx, p, len);
}

void ws_sha1_final(ws_sha1_ctx_t* ctx, uint8_t out_digest[20]) {
    uint8_t pad = 0x80U;
    uint8_t zero = 0x00U;
    uint8_t len_be[8];
    for (int i = 0; i < 8; i++) {
        len_be[7 - i] = (uint8_t)((ctx->count >> (i * 8)) & 0xFFU);
    }

    ws_sha1_update(ctx, &pad, 1);
    while (((ctx->count >> 3) & 63U) != 56U) {
        ws_sha1_update(ctx, &zero, 1);
    }
    ws_sha1_update(ctx, len_be, 8);

    for (int i = 0; i < 5; i++) {
        out_digest[i * 4 + 0] = (uint8_t)((ctx->state[i] >> 24) & 0xFFU);
        out_digest[i * 4 + 1] = (uint8_t)((ctx->state[i] >> 16) & 0xFFU);
        out_digest[i * 4 + 2] = (uint8_t)((ctx->state[i] >> 8) & 0xFFU);
        out_digest[i * 4 + 3] = (uint8_t)((ctx->state[i]) & 0xFFU);
    }
}



#include "base64.h"

static const char k_b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t ws_base64_encode(const unsigned char* in, size_t in_len, char* out, int include_nul) {
    size_t out_len = ((in_len + 2) / 3) * 4;
    size_t total = out_len + (include_nul ? 1 : 0);
    if (!out) return total;

    size_t i = 0, o = 0;
    while (i + 3 <= in_len) {
        unsigned v = (in[i] << 16) | (in[i + 1] << 8) | in[i + 2];
        out[o++] = k_b64[(v >> 18) & 63];
        out[o++] = k_b64[(v >> 12) & 63];
        out[o++] = k_b64[(v >> 6) & 63];
        out[o++] = k_b64[v & 63];
        i += 3;
    }
    if (i + 1 == in_len) {
        unsigned v = (in[i] << 16);
        out[o++] = k_b64[(v >> 18) & 63];
        out[o++] = k_b64[(v >> 12) & 63];
        out[o++] = '=';
        out[o++] = '=';
    } else if (i + 2 == in_len) {
        unsigned v = (in[i] << 16) | (in[i + 1] << 8);
        out[o++] = k_b64[(v >> 18) & 63];
        out[o++] = k_b64[(v >> 12) & 63];
        out[o++] = k_b64[(v >> 6) & 63];
        out[o++] = '=';
    }
    if (include_nul) out[o] = '\0';
    return total;
}



#include "handshake.h"
#include "internal/sha1.h"
#include "internal/base64.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

void ws_compute_accept(const char* base64_key, char out_accept[29]) {
    /* accept = base64( SHA1( key + GUID ) ) */
    char concat[60]; /* 24 (b64 key) + 36 (GUID) = 60 max */
    size_t key_len = strlen(base64_key);
    memcpy(concat, base64_key, key_len);
    memcpy(concat + key_len, WS_GUID, sizeof(WS_GUID) - 1);
    size_t concat_len = key_len + (sizeof(WS_GUID) - 1);

    ws_sha1_ctx_t ctx;
    uint8_t digest[20];
    ws_sha1_init(&ctx);
    ws_sha1_update(&ctx, concat, concat_len);
    ws_sha1_final(&ctx, digest);
    (void)ws_base64_encode(digest, sizeof(digest), out_accept, 1);
}

int ws_generate_client_key(char out_key[25]) {
    /* 16 random bytes -> base64 => 24 chars */
    unsigned char rnd[16];
    /* Use rand() for now; replace with better RNG later */
    srand((unsigned)time(NULL));
    for (int i = 0; i < 16; i++) rnd[i] = (unsigned char)(rand() & 0xFF);
    (void)ws_base64_encode(rnd, sizeof(rnd), out_key, 1);
    return 0;
}

int ws_build_handshake_request(const char* host, int port, const char* path,
                               const char* sec_websocket_key,
                               const char* user_agent,
                               const char* origin,
                               const char* protocol,
                               char* out, size_t out_cap) {
    if (!host || !path || !sec_websocket_key || !out) return -1;
    int n = snprintf(out, out_cap,
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n",
        path, host, port, sec_websocket_key);
    if (n < 0 || (size_t)n >= out_cap) return -1;
    size_t off = (size_t)n;

    if (user_agent && *user_agent) {
        n = snprintf(out + off, out_cap - off, "User-Agent: %s\r\n", user_agent);
        if (n < 0 || (size_t)n >= out_cap - off) return -1;
        off += (size_t)n;
    }
    if (origin && *origin) {
        n = snprintf(out + off, out_cap - off, "Origin: %s\r\n", origin);
        if (n < 0 || (size_t)n >= out_cap - off) return -1;
        off += (size_t)n;
    }
    if (protocol && *protocol) {
        n = snprintf(out + off, out_cap - off, "Sec-WebSocket-Protocol: %s\r\n", protocol);
        if (n < 0 || (size_t)n >= out_cap - off) return -1;
        off += (size_t)n;
    }
    if (off + 2 >= out_cap) return -1;
    memcpy(out + off, "\r\n", 2);
    off += 2;
    return (int)off;
}



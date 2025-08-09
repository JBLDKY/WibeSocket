#include "handshake.h"
#include "internal/sha1.h"
#include "internal/base64.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include <string.h>

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
    /* Prefer getrandom if available; fallback to rand() */
#if defined(__linux__)
    #include <sys/random.h>
    ssize_t r = getrandom(rnd, sizeof(rnd), 0);
    if (r != (ssize_t)sizeof(rnd)) {
        srand((unsigned)time(NULL));
        for (int i = 0; i < 16; i++) rnd[i] = (unsigned char)(rand() & 0xFF);
    }
#else
    srand((unsigned)time(NULL));
    for (int i = 0; i < 16; i++) rnd[i] = (unsigned char)(rand() & 0xFF);
#endif
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

static const char* ws_find_header_ci(const char* resp, const char* header_name) {
    size_t nlen = strlen(header_name);
    const char* p = resp;
    while ((p = strstr(p, header_name)) != NULL) {
        /* Ensure it starts at line start (or after \n) and is followed by ':' */
        const char* line_start = p;
        if (line_start != resp && *(line_start - 1) != '\n') { p++; continue; }
        const char* colon = p + nlen;
        if (*colon != ':') { p++; continue; }
        return colon + 1; /* return after ':' */
    }
    return NULL;
}

static void ws_trim_lws(const char** start, const char** end) {
    const char* s = *start;
    const char* e = *end;
    while (s < e && (*s == ' ' || *s == '\t')) s++;
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) e--;
    *start = s; *end = e;
}

int ws_validate_handshake_response(const char* response, const char* expected_accept) {
    if (!response || !expected_accept) return -1;
    /* Check status line: HTTP/1.1 101 Switching Protocols */
    const char* status = strstr(response, "HTTP/1.1 101");
    if (!status || (status != response && *(status - 1) != '\n')) return -1;

    /* Required headers: Upgrade: websocket, Connection: Upgrade, Sec-WebSocket-Accept: <val> */
    const char* upv = ws_find_header_ci(response, "Upgrade");
    const char* cov = ws_find_header_ci(response, "Connection");
    const char* acv = ws_find_header_ci(response, "Sec-WebSocket-Accept");
    if (!upv || !cov || !acv) return -1;

    /* Extract header values until end-of-line */
    const char* up_end = strchr(upv, '\n'); if (!up_end) return -1;
    const char* co_end = strchr(cov, '\n'); if (!co_end) return -1;
    const char* ac_end = strchr(acv, '\n'); if (!ac_end) return -1;

    ws_trim_lws(&upv, &up_end);
    ws_trim_lws(&cov, &co_end);
    ws_trim_lws(&acv, &ac_end);

    /* Case-insensitive contains checks for Upgrade/Connection */
    /* Portable case-insensitive substring search */
    auto const char* ws_strcasestr(const char* haystack, const char* needle) {
        if (!*needle) return haystack;
        size_t nlen = strlen(needle);
        for (const char* p = haystack; *p; p++) {
            size_t i = 0;
            while (i < nlen) {
                char a = p[i];
                char b = needle[i];
                if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
                if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
                if (a != b) break;
                i++;
            }
            if (i == nlen) return p;
        }
        return NULL;
    }

    const char* up_ws = ws_strcasestr(upv, "websocket");
    const char* co_up = ws_strcasestr(cov, "upgrade");
    if (!up_ws || !co_up) return -1;

    /* Accept must match exactly */
    size_t ac_len = (size_t)(ac_end - acv);
    if (strlen(expected_accept) != ac_len) return -1;
    if (strncmp(acv, expected_accept, ac_len) != 0) return -1;

    return 0;
}



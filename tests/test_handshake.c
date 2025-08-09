/* Basic unit tests for handshake helpers */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "wibesocket/wibesocket.h"
#include "../src/handshake.h"

static void test_accept_known_vector(void) {
    /* Example from RFC 6455: Sec-WebSocket-Key "dGhlIHNhbXBsZSBub25jZQ==" yields
       Sec-WebSocket-Accept "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=" */
    const char* key = "dGhlIHNhbXBsZSBub25jZQ==";
    char accept[29];
    ws_compute_accept(key, accept);
    assert(strcmp(accept, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") == 0);
}

static void test_request_build_minimal(void) {
    char req[512];
    int n = ws_build_handshake_request("example.com", 80, "/chat", "abcd", NULL, NULL, NULL, req, sizeof(req));
    assert(n > 0);
    const char* must[] = {
        "GET /chat HTTP/1.1\r\n",
        "Host: example.com:80\r\n",
        "Upgrade: websocket\r\n",
        "Connection: Upgrade\r\n",
        "Sec-WebSocket-Key: abcd\r\n",
        "Sec-WebSocket-Version: 13\r\n\r\n",
    };
    for (size_t i = 0; i < sizeof(must)/sizeof(must[0]); i++) {
        assert(strstr(req, must[i]) != NULL);
    }
}

static void test_validate_response_ok(void) {
    const char* key = "dGhlIHNhbXBsZSBub25jZQ==";
    char accept[29];
    ws_compute_accept(key, accept);
    const char* resp =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
        "\r\n";
    assert(ws_validate_handshake_response(resp, accept) == 0);
}

static void test_validate_response_fail(void) {
    const char* key = "dGhlIHNhbXBsZSBub25jZQ==";
    char accept[29];
    ws_compute_accept(key, accept);
    const char* resp_bad =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: WRONG\r\n"
        "\r\n";
    assert(ws_validate_handshake_response(resp_bad, accept) != 0);
}

int main(void) {
    test_accept_known_vector();
    test_request_build_minimal();
    test_validate_response_ok();
    test_validate_response_fail();
    printf("test_handshake OK\n");
    return 0;
}


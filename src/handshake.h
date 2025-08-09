#ifndef WIBESOCKET_HANDSHAKE_H
#define WIBESOCKET_HANDSHAKE_H

#include <stddef.h>
#include <stdint.h>

/* RFC 6455 GUID */
#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

/* Computes Sec-WebSocket-Accept value for a given base64 key. */
/* out_accept must have space for 28+1 bytes (base64 of 20 bytes: 28 chars + NUL). */
void ws_compute_accept(const char* base64_key, char out_accept[29]);

/* Generate a 16-byte random key and return it as Base64 string into out_key.
 * out_key must have space for 24+1 bytes (Base64 of 16 bytes). Returns 0 on success. */
int ws_generate_client_key(char out_key[25]);

/* Build a minimal HTTP/1.1 WebSocket Upgrade request into out (size out_cap).
 * Returns length written on success or -1 if insufficient capacity. */
int ws_build_handshake_request(const char* host, int port, const char* path,
                               const char* sec_websocket_key,
                               const char* user_agent,
                               const char* origin,
                               const char* protocol,
                               char* out, size_t out_cap);

#endif /* WIBESOCKET_HANDSHAKE_H */



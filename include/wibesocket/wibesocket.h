#ifndef WIBESOCKET_H
#define WIBESOCKET_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wibesocket_conn wibesocket_conn_t;

typedef enum {
    WIBESOCKET_STATE_INIT = 0,
    WIBESOCKET_STATE_CONNECTING,
    WIBESOCKET_STATE_OPEN,
    WIBESOCKET_STATE_CLOSING,
    WIBESOCKET_STATE_CLOSED,
    WIBESOCKET_STATE_ERROR
} wibesocket_state_t;

typedef enum {
    WIBESOCKET_FRAME_CONTINUATION = 0x0,
    WIBESOCKET_FRAME_TEXT         = 0x1,
    WIBESOCKET_FRAME_BINARY       = 0x2,
    WIBESOCKET_FRAME_CLOSE        = 0x8,
    WIBESOCKET_FRAME_PING         = 0x9,
    WIBESOCKET_FRAME_PONG         = 0xA
} wibesocket_frame_type_t;

typedef enum {
    WIBESOCKET_CLOSE_NORMAL           = 1000,
    WIBESOCKET_CLOSE_GOING_AWAY       = 1001,
    WIBESOCKET_CLOSE_PROTOCOL_ERROR   = 1002,
    WIBESOCKET_CLOSE_UNSUPPORTED_DATA = 1003,
    WIBESOCKET_CLOSE_NO_STATUS        = 1005,
    WIBESOCKET_CLOSE_ABNORMAL         = 1006,
    WIBESOCKET_CLOSE_INVALID_PAYLOAD  = 1007,
    WIBESOCKET_CLOSE_POLICY_VIOLATION = 1008,
    WIBESOCKET_CLOSE_TOO_LARGE        = 1009,
    WIBESOCKET_CLOSE_INTERNAL_ERROR   = 1011
} wibesocket_close_code_t;

typedef struct {
    const char* user_agent;
    const char* origin;
    const char* protocol;
    uint32_t    handshake_timeout_ms;
    uint32_t    max_frame_size;
    bool        enable_compression;
} wibesocket_config_t;

typedef struct {
    wibesocket_frame_type_t type;
    const void*             payload;
    size_t                  payload_len;
    bool                    is_final;
} wibesocket_message_t;

typedef enum {
    WIBESOCKET_OK = 0,
    WIBESOCKET_ERROR_INVALID_ARGS,
    WIBESOCKET_ERROR_MEMORY,
    WIBESOCKET_ERROR_NETWORK,
    WIBESOCKET_ERROR_HANDSHAKE,
    WIBESOCKET_ERROR_PROTOCOL,
    WIBESOCKET_ERROR_TIMEOUT,
    WIBESOCKET_ERROR_CLOSED,
    WIBESOCKET_ERROR_BUFFER_FULL,
    WIBESOCKET_ERROR_NOT_READY
} wibesocket_error_t;

wibesocket_conn_t* wibesocket_connect(const char* uri, const wibesocket_config_t* config);
wibesocket_error_t wibesocket_send_text(wibesocket_conn_t* conn, const char* text, size_t len);
wibesocket_error_t wibesocket_send_binary(wibesocket_conn_t* conn, const void* data, size_t len);
wibesocket_error_t wibesocket_send_ping(wibesocket_conn_t* conn, const void* data, size_t len);
wibesocket_error_t wibesocket_send_close(wibesocket_conn_t* conn, uint16_t code, const char* reason);
wibesocket_error_t wibesocket_recv(wibesocket_conn_t* conn, wibesocket_message_t* msg, int timeout_ms);
wibesocket_state_t wibesocket_get_state(const wibesocket_conn_t* conn);
wibesocket_error_t wibesocket_get_error(const wibesocket_conn_t* conn);
wibesocket_error_t wibesocket_close(wibesocket_conn_t* conn);
const char*        wibesocket_error_string(wibesocket_error_t error);

/* Advanced: zero-copy payload lifetime management for FFI bindings */
void               wibesocket_retain_payload(wibesocket_conn_t* conn);
void               wibesocket_release_payload(wibesocket_conn_t* conn);

/* File descriptor access for event loop integration */
int                wibesocket_fileno(const wibesocket_conn_t* conn);

#ifdef __cplusplus
}
#endif

#endif /* WIBESOCKET_H */

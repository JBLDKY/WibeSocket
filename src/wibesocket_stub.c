#include <stdlib.h>
#include <string.h>

#include "wibesocket/wibesocket.h"

struct wibesocket_conn {
    wibesocket_state_t state;
    wibesocket_error_t last_error;
    int fd;
    int payload_pinned;
};

static const char* k_error_strings[] = {
    "ok",
    "invalid args",
    "memory",
    "network",
    "handshake",
    "protocol",
    "timeout",
    "closed",
    "buffer full",
    "not ready",
};

wibesocket_conn_t* wibesocket_connect(const char* uri, const wibesocket_config_t* config) {
    (void)uri;
    (void)config;
    wibesocket_conn_t* c = (wibesocket_conn_t*)calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->state = WIBESOCKET_STATE_CONNECTING;
    c->last_error = WIBESOCKET_OK;
    c->fd = -1;
    c->payload_pinned = 0;
    return c;
}

wibesocket_error_t wibesocket_send_text(wibesocket_conn_t* conn, const char* text, size_t len) {
    (void)text; (void)len;
    if (!conn) return WIBESOCKET_ERROR_INVALID_ARGS;
    return WIBESOCKET_OK;
}

wibesocket_error_t wibesocket_send_binary(wibesocket_conn_t* conn, const void* data, size_t len) {
    (void)data; (void)len;
    if (!conn) return WIBESOCKET_ERROR_INVALID_ARGS;
    return WIBESOCKET_OK;
}

wibesocket_error_t wibesocket_send_ping(wibesocket_conn_t* conn, const void* data, size_t len) {
    (void)data; (void)len;
    if (!conn) return WIBESOCKET_ERROR_INVALID_ARGS;
    return WIBESOCKET_OK;
}

wibesocket_error_t wibesocket_send_close(wibesocket_conn_t* conn, uint16_t code, const char* reason) {
    (void)code; (void)reason;
    if (!conn) return WIBESOCKET_ERROR_INVALID_ARGS;
    conn->state = WIBESOCKET_STATE_CLOSING;
    return WIBESOCKET_OK;
}

wibesocket_error_t wibesocket_recv(wibesocket_conn_t* conn, wibesocket_message_t* msg, int timeout_ms) {
    (void)msg; (void)timeout_ms;
    if (!conn) return WIBESOCKET_ERROR_INVALID_ARGS;
    return WIBESOCKET_ERROR_TIMEOUT;
}

wibesocket_state_t wibesocket_get_state(const wibesocket_conn_t* conn) {
    if (!conn) return WIBESOCKET_STATE_ERROR;
    return conn->state;
}

wibesocket_error_t wibesocket_get_error(const wibesocket_conn_t* conn) {
    if (!conn) return WIBESOCKET_ERROR_INVALID_ARGS;
    return conn->last_error;
}

wibesocket_error_t wibesocket_close(wibesocket_conn_t* conn) {
    if (!conn) return WIBESOCKET_ERROR_INVALID_ARGS;
    free(conn);
    return WIBESOCKET_OK;
}

const char* wibesocket_error_string(wibesocket_error_t error) {
    size_t n = sizeof(k_error_strings) / sizeof(k_error_strings[0]);
    if ((unsigned)error < n) return k_error_strings[error];
    return "unknown";
}

void wibesocket_retain_payload(wibesocket_conn_t* conn) {
    if (!conn) return;
    conn->payload_pinned = 1;
}

void wibesocket_release_payload(wibesocket_conn_t* conn) {
    if (!conn) return;
    conn->payload_pinned = 0;
}

int wibesocket_fileno(const wibesocket_conn_t* conn) {
    if (!conn) return -1;
    return conn->fd;
}

wibesocket_error_t wibesocket_poll_events(wibesocket_conn_t* conn, int timeout_ms) {
    (void)timeout_ms;
    if (!conn) return WIBESOCKET_ERROR_INVALID_ARGS;
    return WIBESOCKET_ERROR_TIMEOUT;
}

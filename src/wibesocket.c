#define _POSIX_C_SOURCE 200809L
#include "wibesocket/wibesocket.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netdb.h>
#include <time.h>
#include <stdio.h>

#include "internal/frame.h"
#include "internal/ringbuf.h"
#include "handshake.h"

typedef struct wibesocket_conn {
    int                fd;
    int                epfd;
    wibesocket_state_t state;
    wibesocket_error_t last_error;
    wibesocket_config_t cfg;

    /* handshake */
    char client_key[25];
    char expected_accept[29];

    /* recv */
    uint8_t* recv_buf;
    size_t   recv_size;
    size_t   recv_cap;
    size_t   pending_consume;
    ws_parser_t parser;

    /* FFI payload lifetime pinning */
    const uint8_t* pinned_payload;
    size_t         pinned_len;
    int            pinned_refcnt;
} wibesocket_conn;

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return -1;
    return 0;
}

static int parse_ws_uri(const char* uri, char** out_host, char** out_port, char** out_path) {
    /* Very small parser for ws://host[:port]/path */
    const char* p = strstr(uri, "://");
    if (!p) return -1;
    size_t scheme_len = (size_t)(p - uri);
    if (!(scheme_len == 2 && strncmp(uri, "ws", 2) == 0)) return -1;
    p += 3; /* skip :// */
    const char* host_start = p;
    const char* slash = strchr(p, '/');
    const char* host_end = slash ? slash : uri + strlen(uri);
    const char* colon = memchr(host_start, ':', (size_t)(host_end - host_start));
    if (colon) {
        *out_host = strndup(host_start, (size_t)(colon - host_start));
        *out_port = strndup(colon + 1, (size_t)(host_end - (colon + 1)));
    } else {
        *out_host = strndup(host_start, (size_t)(host_end - host_start));
        *out_port = strdup("80");
    }
    *out_path = slash ? strdup(slash) : strdup("/");
    return (*out_host && *out_port && *out_path) ? 0 : -1;
}

static int ep_add_out(int epfd, int fd) {
    struct epoll_event ev; memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLOUT | EPOLLET; ev.data.fd = fd;
    return epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
}

static int ep_mod_in(int epfd, int fd) {
    struct epoll_event ev; memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN | EPOLLET; ev.data.fd = fd;
    return epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
}

static int wait_epoll(int epfd, int timeout_ms) {
    struct epoll_event ev; return epoll_wait(epfd, &ev, 1, timeout_ms);
}

static int socket_connect_nb(const char* host, const char* port) {
    struct addrinfo hints; memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = NULL;
    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) return -1;
    int fd = -1;
    for (struct addrinfo* ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype | SOCK_NONBLOCK, ai->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
        if (errno == EINPROGRESS) break;
        close(fd); fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

static wibesocket_error_t do_handshake(wibesocket_conn* c, const char* host, int port, const char* path) {
    if (ws_generate_client_key(c->client_key) != 0) return WIBESOCKET_ERROR_HANDSHAKE;
    ws_compute_accept(c->client_key, c->expected_accept);
    char req[1024];
    int n = ws_build_handshake_request(host, port, path, c->client_key,
                                       c->cfg.user_agent, c->cfg.origin, c->cfg.protocol,
                                       req, sizeof(req));
    if (n <= 0) return WIBESOCKET_ERROR_HANDSHAKE;
    ssize_t wr = send(c->fd, req, (size_t)n, 0);
    if (wr < 0 && errno != EAGAIN && errno != EWOULDBLOCK) return WIBESOCKET_ERROR_NETWORK;

    /* wait for response */
    int timeout = (int)c->cfg.handshake_timeout_ms;
    if (timeout <= 0) timeout = 5000;
    char resp[4096]; size_t resp_len = 0;
    for (;;) {
        int w = wait_epoll(c->epfd, timeout);
        if (w <= 0) return WIBESOCKET_ERROR_TIMEOUT;
        ssize_t rd = recv(c->fd, resp + resp_len, sizeof(resp) - 1 - resp_len, 0);
        if (rd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            return WIBESOCKET_ERROR_NETWORK;
        }
        if (rd == 0) return WIBESOCKET_ERROR_NETWORK;
        resp_len += (size_t)rd; resp[resp_len] = '\0';
        if (strstr(resp, "\r\n\r\n")) break;
        if (resp_len >= sizeof(resp) - 1) return WIBESOCKET_ERROR_HANDSHAKE;
    }
    if (ws_validate_handshake_response(resp, c->expected_accept) != 0) return WIBESOCKET_ERROR_HANDSHAKE;
    return WIBESOCKET_OK;
}

wibesocket_conn_t* wibesocket_connect(const char* uri, const wibesocket_config_t* config) {
    if (!uri) return NULL;
    char *host = NULL, *port = NULL, *path = NULL;
    if (parse_ws_uri(uri, &host, &port, &path) != 0) return NULL;

    wibesocket_conn* c = (wibesocket_conn*)calloc(1, sizeof(*c));
    if (!c) { free(host); free(port); free(path); return NULL; }
    if (config) c->cfg = *config;
    c->state = WIBESOCKET_STATE_CONNECTING;
    c->last_error = WIBESOCKET_OK;
    c->recv_cap = (c->cfg.max_frame_size ? c->cfg.max_frame_size : (1U << 20)) + 16;
    c->recv_buf = (uint8_t*)malloc(c->recv_cap);
    if (!c->recv_buf) { free(c); free(host); free(port); free(path); return NULL; }
    ws_parser_init(&c->parser, c->cfg.max_frame_size ? c->cfg.max_frame_size : (1U << 20));

    c->fd = socket_connect_nb(host, port);
    if (c->fd < 0) { c->last_error = WIBESOCKET_ERROR_NETWORK; goto fail; }
    c->epfd = epoll_create1(EPOLL_CLOEXEC);
    if (c->epfd < 0) { c->last_error = WIBESOCKET_ERROR_NETWORK; goto fail; }
    if (ep_add_out(c->epfd, c->fd) < 0) { c->last_error = WIBESOCKET_ERROR_NETWORK; goto fail; }

    int timeout = (int)c->cfg.handshake_timeout_ms; if (timeout <= 0) timeout = 5000;
    int w = wait_epoll(c->epfd, timeout);
    if (w <= 0) { c->last_error = WIBESOCKET_ERROR_TIMEOUT; goto fail; }
    /* switch to read */
    if (ep_mod_in(c->epfd, c->fd) < 0) { c->last_error = WIBESOCKET_ERROR_NETWORK; goto fail; }

    wibesocket_error_t hs = do_handshake(c, host, atoi(port), path);
    if (hs != WIBESOCKET_OK) { c->last_error = hs; goto fail; }

    c->state = WIBESOCKET_STATE_OPEN;
    free(host); free(port); free(path);
    return c;
fail:
    free(host); free(port); free(path);
    (void)wibesocket_close((wibesocket_conn_t*)c);
    return NULL;
}

static void gen_mask(uint8_t m[4]) {
#if defined(__linux__)
    ssize_t r = getrandom(m, 4, 0);
    if (r == 4) return;
#endif
    unsigned x = (unsigned)time(NULL);
    for (int i = 0; i < 4; i++) { x = x * 1103515245u + 12345u; m[i] = (uint8_t)(x >> 24); }
}

static wibesocket_error_t send_frame(wibesocket_conn* c, ws_opcode_t opcode, const void* data, size_t len) {
    if (!c || c->state != WIBESOCKET_STATE_OPEN) return WIBESOCKET_ERROR_NOT_READY;
    uint8_t mask[4]; gen_mask(mask);
    size_t hdr = 2 + ((len <= 125) ? 0 : (len <= 0xFFFF ? 2 : 8)) + 4;
    size_t need = hdr + len;
    uint8_t* buf = (need <= 16384) ? (uint8_t*)alloca(need) : (uint8_t*)malloc(need);
    if (!buf) return WIBESOCKET_ERROR_MEMORY;
    size_t n = ws_build_frame(buf, need, 1, opcode, mask, (const uint8_t*)data, len);
    if (n == 0) { if (buf != (uint8_t*)alloca(0)) free(buf); return WIBESOCKET_ERROR_BUFFER_FULL; }
    ssize_t wr = send(c->fd, buf, n, 0);
    if (buf != (uint8_t*)alloca(0)) free(buf);
    if (wr < 0) return WIBESOCKET_ERROR_NETWORK;
    return WIBESOCKET_OK;
}

wibesocket_error_t wibesocket_send_text(wibesocket_conn_t* conn, const char* text, size_t len) {
    return send_frame((wibesocket_conn*)conn, WS_OPCODE_TEXT, text, len);
}

wibesocket_error_t wibesocket_send_binary(wibesocket_conn_t* conn, const void* data, size_t len) {
    return send_frame((wibesocket_conn*)conn, WS_OPCODE_BINARY, data, len);
}

wibesocket_error_t wibesocket_send_ping(wibesocket_conn_t* conn, const void* data, size_t len) {
    return send_frame((wibesocket_conn*)conn, WS_OPCODE_PING, data, len);
}

wibesocket_error_t wibesocket_send_close(wibesocket_conn_t* conn, uint16_t code, const char* reason) {
    uint8_t payload[2 + 125]; size_t n = 0;
    payload[n++] = (uint8_t)((code >> 8) & 0xFF); payload[n++] = (uint8_t)(code & 0xFF);
    if (reason) {
        size_t rlen = strlen(reason); if (rlen > 125) rlen = 125;
        memcpy(payload + n, reason, rlen); n += rlen;
    }
    wibesocket_error_t e = send_frame((wibesocket_conn*)conn, WS_OPCODE_CLOSE, payload, n);
    if (e == WIBESOCKET_OK) ((wibesocket_conn*)conn)->state = WIBESOCKET_STATE_CLOSING;
    return e;
}

wibesocket_error_t wibesocket_recv(wibesocket_conn_t* conn, wibesocket_message_t* msg, int timeout_ms) {
    wibesocket_conn* c = (wibesocket_conn*)conn;
    if (!c || c->state != WIBESOCKET_STATE_OPEN) return WIBESOCKET_ERROR_NOT_READY;
    if (c->pinned_refcnt > 0) return WIBESOCKET_ERROR_NOT_READY;
    int w = wait_epoll(c->epfd, timeout_ms);
    if (w <= 0) return WIBESOCKET_ERROR_TIMEOUT;
    ssize_t rd = recv(c->fd, c->recv_buf + c->recv_size, c->recv_cap - c->recv_size, 0);
    if (rd < 0) return (errno == EAGAIN || errno == EWOULDBLOCK) ? WIBESOCKET_ERROR_TIMEOUT : WIBESOCKET_ERROR_NETWORK;
    if (rd == 0) { c->state = WIBESOCKET_STATE_CLOSED; return WIBESOCKET_ERROR_CLOSED; }
    c->recv_size += (size_t)rd;

    size_t consumed = 0; ws_parsed_frame_t fr;
    ws_parser_status_t st = ws_parser_feed(&c->parser, c->recv_buf, c->recv_size, &consumed, &fr);
    if (st == WS_PARSER_NEED_MORE) return WIBESOCKET_ERROR_NOT_READY;
    if (st < 0) { c->last_error = WIBESOCKET_ERROR_PROTOCOL; return c->last_error; }
    /* Defer sliding until payload released to keep zero-copy pointer valid */
    c->pending_consume = consumed;

    /* Handle control frames */
    if (fr.type == WS_OPCODE_PING) {
        (void)wibesocket_send_ping(conn, fr.payload, fr.payload_len); /* echo ping -> pong using same API */
        return WIBESOCKET_ERROR_NOT_READY;
    }
    if (fr.type == WS_OPCODE_CLOSE) {
        (void)wibesocket_send_close(conn, WIBESOCKET_CLOSE_NORMAL, "");
        c->state = WIBESOCKET_STATE_CLOSED;
        return WIBESOCKET_ERROR_CLOSED;
    }

    /* Fill out message; zero-copy view into recv buffer */
    msg->type = (fr.type == WS_OPCODE_TEXT) ? WIBESOCKET_FRAME_TEXT :
                (fr.type == WS_OPCODE_BINARY) ? WIBESOCKET_FRAME_BINARY : WIBESOCKET_FRAME_CONTINUATION;
    msg->payload = fr.payload;
    msg->payload_len = fr.payload_len;
    msg->is_final = fr.is_final;
    /* Pin the payload region to avoid reuse/memmove until released by FFI */
    c->pinned_payload = (const uint8_t*)fr.payload;
    c->pinned_len = fr.payload_len;
    c->pinned_refcnt = 1;
    return WIBESOCKET_OK;
}

wibesocket_state_t wibesocket_get_state(const wibesocket_conn_t* conn) {
    const wibesocket_conn* c = (const wibesocket_conn*)conn;
    return c ? c->state : WIBESOCKET_STATE_ERROR;
}

wibesocket_error_t wibesocket_get_error(const wibesocket_conn_t* conn) {
    const wibesocket_conn* c = (const wibesocket_conn*)conn;
    return c ? c->last_error : WIBESOCKET_ERROR_INVALID_ARGS;
}

static void safe_close(int* fd) { if (*fd >= 0) { close(*fd); *fd = -1; } }

wibesocket_error_t wibesocket_close(wibesocket_conn_t* conn) {
    wibesocket_conn* c = (wibesocket_conn*)conn;
    if (!c) return WIBESOCKET_ERROR_INVALID_ARGS;
    safe_close(&c->fd);
    safe_close(&c->epfd);
    free(c->recv_buf);
    free(c);
    return WIBESOCKET_OK;
}

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

const char* wibesocket_error_string(wibesocket_error_t error) {
    size_t n = sizeof(k_error_strings)/sizeof(k_error_strings[0]);
    return ((unsigned)error < n) ? k_error_strings[error] : "unknown";
}

void wibesocket_retain_payload(wibesocket_conn_t* conn) {
    wibesocket_conn* c = (wibesocket_conn*)conn;
    if (!c) return;
    if (c->pinned_refcnt > 0) c->pinned_refcnt++;
}

void wibesocket_release_payload(wibesocket_conn_t* conn) {
    wibesocket_conn* c = (wibesocket_conn*)conn;
    if (!c) return;
    if (c->pinned_refcnt > 0) c->pinned_refcnt--;
    if (c->pinned_refcnt == 0) {
        /* After release, compact recv buffer by consuming the parsed frame bytes */
        if (c->pending_consume > 0 && c->pending_consume <= c->recv_size) {
            memmove(c->recv_buf, c->recv_buf + c->pending_consume, c->recv_size - c->pending_consume);
            c->recv_size -= c->pending_consume;
            c->pending_consume = 0;
        }
        c->pinned_payload = NULL;
        c->pinned_len = 0;
    }
}

int wibesocket_fileno(const wibesocket_conn_t* conn) {
    const wibesocket_conn* c = (const wibesocket_conn*)conn;
    return c ? c->fd : -1;
}



#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wibesocket/wibesocket.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s ws://host:port/path\n", argv[0]);
        return 2;
    }

    const char* uri = argv[1];

    wibesocket_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.handshake_timeout_ms = 5000;
    cfg.max_frame_size = 1 << 20; /* 1 MiB */
    cfg.enable_compression = false;

    wibesocket_conn_t* conn = wibesocket_connect(uri, &cfg);
    if (!conn) {
        fprintf(stderr, "connect failed\n");
        return 1;
    }

    const char* hello = "hello from simple_echo";
    wibesocket_error_t se = wibesocket_send_text(conn, hello, strlen(hello));
    if (se != WIBESOCKET_OK) {
        fprintf(stderr, "send error: %s\n", wibesocket_error_string(se));
    }

    for (;;) {
        wibesocket_message_t msg;
        memset(&msg, 0, sizeof(msg));
        wibesocket_error_t re = wibesocket_recv(conn, &msg, 3000);
        if (re == WIBESOCKET_OK) {
            if (msg.type == WIBESOCKET_FRAME_TEXT || msg.type == WIBESOCKET_FRAME_BINARY) {
                printf("recv (%s)%s%zu bytes\n",
                       msg.type == WIBESOCKET_FRAME_TEXT ? "text" : "binary",
                       msg.is_final ? " final " : " ",
                       msg.payload_len);
            } else if (msg.type == WIBESOCKET_FRAME_PING) {
                /* Pong handling is internal or not exposed via API; ignoring ping here. */
            } else if (msg.type == WIBESOCKET_FRAME_CLOSE) {
                break;
            }
        } else if (re == WIBESOCKET_ERROR_TIMEOUT) {
            break;
        } else {
            fprintf(stderr, "recv error: %s\n", wibesocket_error_string(re));
            break;
        }
    }

    (void)wibesocket_send_close(conn, WIBESOCKET_CLOSE_NORMAL, "bye");
    (void)wibesocket_close(conn);
    return 0;
}



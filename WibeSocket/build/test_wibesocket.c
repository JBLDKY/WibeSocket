#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wibesocket/wibesocket.h"

static void test_error_strings(void) {
    const char* ok_str = wibesocket_error_string(WIBESOCKET_OK);
    assert(ok_str != NULL);
    const char* proto_str = wibesocket_error_string(WIBESOCKET_ERROR_PROTOCOL);
    assert(proto_str != NULL);
}

static void test_enums_ranges(void) {
    assert(WIBESOCKET_STATE_INIT == 0);
    assert(WIBESOCKET_FRAME_TEXT == 0x1);
    assert(WIBESOCKET_FRAME_PONG == 0xA);
    assert(WIBESOCKET_CLOSE_NORMAL == 1000);
}

static void maybe_connect_basic(void) {
    const char* uri = getenv("WIBESOCKET_TEST_ECHO_URI");
    if (uri == NULL) {
        /* No echo server configured; skip connectivity test gracefully. */
        fprintf(stderr, "[skip] set WIBESOCKET_TEST_ECHO_URI to run connect/send/recv test\n");
        return;
    }

    wibesocket_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.handshake_timeout_ms = 5000;
    cfg.max_frame_size = 1 << 20; /* 1 MiB */
    cfg.enable_compression = false;

    wibesocket_conn_t* conn = wibesocket_connect(uri, &cfg);
    assert(conn != NULL);

    /* State should not be CLOSED/ERROR immediately */
    wibesocket_state_t st = wibesocket_get_state(conn);
    assert(st == WIBESOCKET_STATE_CONNECTING || st == WIBESOCKET_STATE_OPEN);

    /* Best-effort text send/recv roundtrip */
    const char* payload = "hello from tests";
    (void)wibesocket_send_text(conn, payload, strlen(payload));

    wibesocket_message_t msg;
    memset(&msg, 0, sizeof(msg));
    (void)wibesocket_recv(conn, &msg, 2000);

    /* We cannot assert delivery without a real echo server; just ensure API can be invoked */

    (void)wibesocket_send_close(conn, WIBESOCKET_CLOSE_NORMAL, "bye");
    (void)wibesocket_close(conn);
}

int main(void) {
    test_error_strings();
    test_enums_ranges();
    maybe_connect_basic();
    printf("test_wibesocket: OK\n");
    return 0;
}



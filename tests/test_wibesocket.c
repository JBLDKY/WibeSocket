#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wibesocket/wibesocket.h"

int main(void) {
    /* Basic API presence checks */
    assert(wibesocket_error_string(WIBESOCKET_OK) != NULL);
    assert(WIBESOCKET_FRAME_TEXT == 0x1);
    assert(WIBESOCKET_CLOSE_NORMAL == 1000);

    /* Optional smoke connect if env set */
    const char* uri = getenv("WIBESOCKET_TEST_ECHO_URI");
    if (uri) {
        wibesocket_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.handshake_timeout_ms = 1000;
        cfg.max_frame_size = 1 << 20;

        wibesocket_conn_t* c = wibesocket_connect(uri, &cfg);
        if (c) {
            (void)wibesocket_send_close(c, WIBESOCKET_CLOSE_NORMAL, "bye");
            (void)wibesocket_close(c);
        }
    } else {
        fprintf(stderr, "[skip] set WIBESOCKET_TEST_ECHO_URI to run smoke connect.\n");
    }

    printf("test_wibesocket OK\n");
    return 0;
}



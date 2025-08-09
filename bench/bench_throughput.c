#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "wibesocket/wibesocket.h"

static uint64_t now_ms(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return (uint64_t)ts.tv_sec*1000ull+ts.tv_nsec/1000000ull; }

int main(int argc, char** argv) {
    const char* uri = argc > 1 ? argv[1] : getenv("WIBESOCKET_BENCH_URI");
    size_t msg_len = (argc > 2) ? (size_t)strtoul(argv[2], NULL, 10) : 125;
    size_t num = (argc > 3) ? (size_t)strtoul(argv[3], NULL, 10) : 100000;
    if (!uri) { fprintf(stderr, "usage: %s ws://host:port/path [len] [count]\n", argv[0]); return 2; }

    wibesocket_config_t cfg = {0}; cfg.handshake_timeout_ms = 5000; cfg.max_frame_size = (1u<<20);
    wibesocket_conn_t* c = wibesocket_connect(uri, &cfg);
    if (!c) { fprintf(stderr, "connect failed\n"); return 1; }

    char* payload = (char*)malloc(msg_len); memset(payload, 'A', msg_len);
    uint64_t start = now_ms();
    for (size_t i = 0; i < num; i++) {
        (void)wibesocket_send_binary(c, payload, msg_len);
    }
    uint64_t end = now_ms();
    double secs = (double)(end - start) / 1000.0;
    double throughput = (secs > 0.0) ? (double)num / secs : 0.0;
    printf("len=%zu count=%zu time=%.3fs msgs/s=%.2f\n", msg_len, num, secs, throughput);
    free(payload);
    (void)wibesocket_close(c);
    return 0;
}



#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "wibesocket/wibesocket.h"

static uint64_t now_ns(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return (uint64_t)ts.tv_sec*1000000000ull+ts.tv_nsec; }

int main(int argc, char** argv) {
    const char* uri = argc > 1 ? argv[1] : getenv("WIBESOCKET_BENCH_URI");
    size_t iters = (argc > 2) ? (size_t)strtoul(argv[2], NULL, 10) : 10000;
    if (!uri) { fprintf(stderr, "usage: %s ws://host:port/path [iters]\n", argv[0]); return 2; }

    wibesocket_config_t cfg = {0}; cfg.handshake_timeout_ms = 5000; cfg.max_frame_size = (1u<<20);
    wibesocket_conn_t* c = wibesocket_connect(uri, &cfg);
    if (!c) { fprintf(stderr, "connect failed\n"); return 1; }

    const char* ping = "x";
    uint64_t* samples = (uint64_t*)malloc(iters * sizeof(uint64_t));
    size_t n = 0;
    for (size_t i = 0; i < iters; i++) {
        uint64_t t0 = now_ns();
        (void)wibesocket_send_text(c, ping, 1);
        wibesocket_message_t msg; memset(&msg, 0, sizeof(msg));
        (void)wibesocket_recv(c, &msg, 1000);
        uint64_t t1 = now_ns();
        if (n < iters) samples[n++] = (t1 - t0);
    }
    /* compute p50/p90/p99 */
    for (size_t i = 0; i + 1 < n; i++) {
        for (size_t j = i + 1; j < n; j++) if (samples[j] < samples[i]) { uint64_t tmp = samples[i]; samples[i] = samples[j]; samples[j] = tmp; }
    }
    uint64_t p50 = samples[(size_t)(n * 0.50)];
    uint64_t p90 = samples[(size_t)(n * 0.90)];
    uint64_t p99 = samples[(size_t)(n * 0.99)];
    printf("latency: p50=%.3fms p90=%.3fms p99=%.3fms\n", p50/1e6, p90/1e6, p99/1e6);
    free(samples);
    (void)wibesocket_close(c);
    return 0;
}



#include <libwebsockets.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static volatile int completed = 0;
static size_t messages_left = 0;
static size_t payload_len = 0;
static unsigned char *payload = NULL;

static uint64_t now_ms(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return (uint64_t)ts.tv_sec*1000ull+ts.tv_nsec/1000000ull; }

static int callback_minimal(struct lws *wsi, enum lws_callback_reasons reason,
                            void *user, void *in, size_t len)
{
    switch (reason) {
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        lws_callback_on_writable(wsi);
        break;
    case LWS_CALLBACK_CLIENT_WRITEABLE:
        if (messages_left) {
            unsigned char buf[LWS_PRE + 65536];
            memcpy(buf + LWS_PRE, payload, payload_len);
            lws_write(wsi, buf + LWS_PRE, payload_len, LWS_WRITE_BINARY);
        }
        break;
    case LWS_CALLBACK_CLIENT_RECEIVE:
        if (messages_left) {
            messages_left--;
            if (messages_left)
                lws_callback_on_writable(wsi);
            else
                completed = 1;
        }
        break;
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
    case LWS_CALLBACK_CLOSED:
        completed = 1;
        break;
    default:
        break;
    }
    return 0;
}

static struct lws_protocols protocols[] = {
    { "bench", callback_minimal, 0, 0, 0, NULL, 0 },
    { NULL, NULL, 0, 0, 0, NULL, 0 }
};

int main(int argc, char **argv)
{
    const char *uri = argc > 1 ? argv[1] : NULL;
    if (!uri) {
        fprintf(stderr, "usage: %s ws://host:port/path [len] [count]\n", argv[0]);
        return 2;
    }
    payload_len = (size_t)(argc > 2 ? strtoul(argv[2], NULL, 10) : 125);
    messages_left = (size_t)(argc > 3 ? strtoul(argv[3], NULL, 10) : 10000);
    payload = malloc(payload_len);
    memset(payload, 'A', payload_len);

    struct lws_context_creation_info info; memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    struct lws_context *context = lws_create_context(&info);
    if (!context) {
        fprintf(stderr, "lws context failed\n");
        return 1;
    }

    struct lws_client_connect_info ccinfo; memset(&ccinfo, 0, sizeof(ccinfo));
    ccinfo.context = context;
    ccinfo.address = NULL;
    ccinfo.path = NULL;
    ccinfo.port = 0;
    ccinfo.host = NULL;
    ccinfo.origin = NULL;
    ccinfo.ssl_connection = 0;
    ccinfo.protocol = protocols[0].name;
    ccinfo.alpn = NULL;
    ccinfo.vhost = lws_get_vhost_by_name(context, "default");
    ccinfo.pwsi = NULL;
    ccinfo.url = uri;

    struct lws *wsi = lws_client_connect_via_info(&ccinfo);
    if (!wsi) {
        fprintf(stderr, "connect failed\n");
        lws_context_destroy(context);
        return 1;
    }

    uint64_t start = now_ms();
    while (!completed)
        lws_service(context, 0);
    uint64_t end = now_ms();
    double secs = (double)(end - start) / 1000.0;
    double thr = messages_left ? 0.0 : ((double)(argc > 3 ? strtoul(argv[3], NULL, 10) : 10000) / secs);
    printf("lws msgs/s=%.2f\n", thr);

    lws_context_destroy(context);
    free(payload);
    return 0;
}



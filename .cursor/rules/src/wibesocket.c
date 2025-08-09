#include "ultraws.h"
struct ultraws { int fd; /* more state here */ };

ultraws_t* ultraws_connect(const char* uri) { return NULL; }
int ultraws_send(ultraws_t* ws, const void* data, size_t len) { return -1; }
int ultraws_recv(ultraws_t* ws, void* buf, size_t buf_size) { return -1; }
int ultraws_close(ultraws_t* ws) { return -1; }

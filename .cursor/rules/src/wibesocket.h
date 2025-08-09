#ifndef ULTRAWS_H
#define ULTRAWS_H

#include <stddef.h>
#include <stdint.h>

typedef struct ultraws ultraws_t;

ultraws_t* ultraws_connect(const char* uri);
int ultraws_send(ultraws_t* ws, const void* data, size_t len);
int ultraws_recv(ultraws_t* ws, void* buf, size_t buf_size);
int ultraws_close(ultraws_t* ws);

#endif

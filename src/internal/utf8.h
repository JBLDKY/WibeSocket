#ifndef WIBESOCKET_INTERNAL_UTF8_H
#define WIBESOCKET_INTERNAL_UTF8_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* Validate UTF-8 per RFC 3629 (no surrogates, max U+10FFFF). */
bool ws_utf8_is_valid(const uint8_t* data, size_t len);

#endif /* WIBESOCKET_INTERNAL_UTF8_H */



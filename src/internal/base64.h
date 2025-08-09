#ifndef WIBESOCKET_INTERNAL_BASE64_H
#define WIBESOCKET_INTERNAL_BASE64_H

#include <stddef.h>

/* Encodes input into Base64 without newlines. Returns length of output written.
 * If out is NULL, returns the required output size (including NUL if include_nul=true).
 */
size_t ws_base64_encode(const unsigned char* in, size_t in_len, char* out, int include_nul);

#endif /* WIBESOCKET_INTERNAL_BASE64_H */



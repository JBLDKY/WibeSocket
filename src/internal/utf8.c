#include "utf8.h"

bool ws_utf8_is_valid(const uint8_t* s, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint8_t c = s[i++];
        if (c < 0x80) continue; /* ASCII */
        if ((c >> 5) == 0x6) {
            if (i >= len) return false; /* need 1 cont */
            uint8_t c1 = s[i++];
            if ((c1 & 0xC0) != 0x80) return false;
            uint32_t cp = ((uint32_t)(c & 0x1F) << 6) | (uint32_t)(c1 & 0x3F);
            if (cp < 0x80) return false; /* overlong */
        } else if ((c >> 4) == 0xE) {
            if (i + 1 >= len) return false; /* need 2 cont */
            uint8_t c1 = s[i++]; uint8_t c2 = s[i++];
            if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) return false;
            uint32_t cp = ((uint32_t)(c & 0x0F) << 12) | ((uint32_t)(c1 & 0x3F) << 6) | (uint32_t)(c2 & 0x3F);
            if (cp < 0x800) return false; /* overlong */
            if (cp >= 0xD800 && cp <= 0xDFFF) return false; /* surrogates */
        } else if ((c >> 3) == 0x1E) {
            if (i + 2 >= len) return false; /* need 3 cont */
            uint8_t c1 = s[i++], c2 = s[i++], c3 = s[i++];
            if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) return false;
            uint32_t cp = ((uint32_t)(c & 0x07) << 18) | ((uint32_t)(c1 & 0x3F) << 12) |
                          ((uint32_t)(c2 & 0x3F) << 6) | (uint32_t)(c3 & 0x3F);
            if (cp < 0x10000) return false; /* overlong */
            if (cp > 0x10FFFF) return false;
        } else {
            return false;
        }
    }
    return true;
}



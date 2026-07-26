#include "b64.h"

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t b64_encode(const uint8_t *src, size_t n, char *dst) {
    size_t o = 0;
    size_t i = 0;
    for (; i + 3 <= n; i += 3) {
        uint32_t v = ((uint32_t)src[i] << 16) |
                     ((uint32_t)src[i + 1] << 8) |
                     ((uint32_t)src[i + 2]);
        dst[o++] = b64_table[(v >> 18) & 0x3F];
        dst[o++] = b64_table[(v >> 12) & 0x3F];
        dst[o++] = b64_table[(v >> 6) & 0x3F];
        dst[o++] = b64_table[v & 0x3F];
    }
    size_t rem = n - i;
    if (rem) {
        uint32_t v = (uint32_t)src[i] << 16;
        if (rem == 2) v |= (uint32_t)src[i + 1] << 8;
        dst[o++] = b64_table[(v >> 18) & 0x3F];
        dst[o++] = b64_table[(v >> 12) & 0x3F];
        dst[o++] = (rem == 2) ? b64_table[(v >> 6) & 0x3F] : '=';
        dst[o++] = '=';
    }
    return o;
}

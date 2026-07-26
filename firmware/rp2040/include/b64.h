#ifndef VBFC_B64_H
#define VBFC_B64_H

#include <stdint.h>
#include <stddef.h>

/* Encodes `n` bytes from `src` into standard base64 at `dst`. Returns the
 * number of chars written (not NUL-terminated). `dst` must hold at least
 * 4*((n+2)/3) chars. Pure, no allocation — safe for firmware. */
size_t b64_encode(const uint8_t *src, size_t n, char *dst);

#endif /* VBFC_B64_H */

#ifndef VBFC_CRC_H
#define VBFC_CRC_H

#include <stdint.h>
#include <stddef.h>

/* IEEE 802.3 CRC-32 (reflected, poly 0xEDB88320, init 0, final xor 0).
 * Matches zlib.crc32 — so host-side verification uses Python's zlib.crc32. */
uint32_t crc32_init(void);
uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t n);
uint32_t crc32_final(uint32_t crc);
uint32_t crc32_full(const uint8_t *data, size_t n);

#endif /* VBFC_CRC_H */

#include "vbfc_crc.h"
#include <stdbool.h>

/* Standard reflected CRC-32. Uses a 256-entry table built once (lazy) so the
 * bit-by-bit loop is the same one zlib uses (zlib.crc32 == this on bytes). */
static uint32_t g_table[256];
static bool g_ready = false;

static void table_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) {
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        g_table[i] = c;
    }
    g_ready = true;
}

uint32_t crc32_init(void) { return 0; }

uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t n) {
    if (!g_ready) table_init();
    crc ^= 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        crc = g_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

uint32_t crc32_final(uint32_t crc) { return crc; }

uint32_t crc32_full(const uint8_t *data, size_t n) {
    return crc32_update(crc32_init(), data, n);
}

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "sniffer.h"
#include "b64.h"
#include "vbfc_crc.h"

static uint8_t  g_ring[SNIFF_RING_BYTES];
static uint32_t g_head = 0;     /* next write slot, in bytes */
static uint32_t g_count = 0;    /* events currently buffered */
static bool     g_active = false;

/* Snapshot + base64 scratch live in BSS, NOT on the stack: sniffer_dump() is
 * called deep in the usb_service_poll -> handle_line -> cmd_sniff chain, and the
 * RP2040 has only 2 KB of stack by default. A 4 KB snap[] frame there would
 * hard-fault before reading a single byte. g_active is frozen for the whole
 * dump (see below) so reuse between dumps is safe — there is no reentrancy. */
static uint8_t g_snap[SNIFF_RING_BYTES];
static char    g_b64[1024 + 1];

void sniffer_init(void) {
    g_head = 0;
    g_count = 0;
    g_active = false;
}

void sniffer_start(void) { g_active = true; }
void sniffer_stop(void)  { g_active = false; }
bool sniffer_is_active(void) { return g_active; }

uint32_t sniffer_count(void)    { return g_count; }
uint32_t sniffer_capacity(void) { return SNIFF_MAX_EVENTS; }

void sniffer_clear(void) {
    g_head = 0;
    g_count = 0;
}

/* Pack an 8-byte event little-endian into the ring, overwriting the oldest
 * entry when full (the ring is a fixed window, not unbounded). */
void sniffer_record(uint8_t cmd, uint8_t flags, uint32_t addr, uint16_t data_bytes) {
    if (!g_active) return;
    uint8_t ev[SNIFF_EVENT_SIZE];
    ev[0] = cmd;
    ev[1] = flags;
    ev[2] = (uint8_t)(addr & 0xFF);
    ev[3] = (uint8_t)((addr >> 8) & 0xFF);
    ev[4] = (uint8_t)((addr >> 16) & 0xFF);
    ev[5] = 0;
    ev[6] = (uint8_t)(data_bytes & 0xFF);
    ev[7] = (uint8_t)((data_bytes >> 8) & 0xFF);

    memcpy(&g_ring[g_head], ev, SNIFF_EVENT_SIZE);
    g_head += SNIFF_EVENT_SIZE;
    if (g_head >= SNIFF_RING_BYTES) g_head = 0;

    if (g_count < SNIFF_MAX_EVENTS) g_count++;
}

/* Linearize the ring (oldest first) into a contiguous snapshot buffer, then
 * base64-encode in CHUNK slices. We chunk the whole snapshot at once since
 * events are tiny and the host expects a single self-contained dump. */
void sniffer_dump(void) {
    g_active = false;   /* freeze the ring while we drain it */

    uint32_t total = g_count * SNIFF_EVENT_SIZE;
    if (total) {
        /* oldest = g_head if full, else index 0 */
        uint32_t start = (g_count == SNIFF_MAX_EVENTS) ? g_head : 0;
        for (uint32_t i = 0; i < total; i++) {
            g_snap[i] = g_ring[(start + i) % SNIFF_RING_BYTES];
        }
    }

    printf("DUMP SNIFF START %lu\r\n", (unsigned long)total);

    uint32_t crc = 0;
    uint32_t off = 0;
    while (off < total) {
        uint32_t n = total - off;
        if (n > 768) n = 768;
        size_t k = b64_encode(&g_snap[off], n, g_b64);
        g_b64[k] = '\0';
        crc = crc32_update(crc, &g_snap[off], n);
        printf("DUMP CHUNK %s\r\n", g_b64);
        off += n;
    }
    printf("DUMP DONE 0x%08lX\r\n", (unsigned long)crc);

    sniffer_clear();   /* drained */
}

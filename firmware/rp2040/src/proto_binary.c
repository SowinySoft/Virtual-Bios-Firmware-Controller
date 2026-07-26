#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "pico/stdlib.h"

#include "proto_binary.h"
#include "vbfc_crc.h"
#include "b64.h"        /* b64_encode used by dump_range; was implicit before */
#include "ext_flash.h"
#include "orig_flash.h"
#include "sniffer.h"   /* for DUMP SNIFF */
#include "image_check.h" /* signature verification at ULOAD DONE */

/* Raw payload per chunk. Base64 expands 3:4, so 768 -> 1024 chars; that plus
 * the "ULOAD CHUNK " / "DUMP CHUNK " prefix fits in a modest line buffer. */
#define CHUNK_RAW   768
#define CHUNK_B64   ((CHUNK_RAW * 4) / 3)   /* 1024 */

static uint8_t g_upbuf[CHUNK_RAW];
static char    g_b64buf[CHUNK_B64 + 1];

/* in-flight upload state */
static struct {
    bool     active;
    uint32_t dest_offset;     /* ext-flash offset */
    uint32_t cursor;           /* next write offset */
    uint32_t total_len;        /* expected total bytes */
    uint32_t received;         /* bytes received so far */
    uint32_t crc;              /* running crc of decoded bytes */
} g_up;

void binary_init(void) {
    memset(&g_up, 0, sizeof(g_up));
}

void binary_reset(void) {
    g_up.active = false;
    g_up.cursor = 0;
    g_up.received = 0;
    g_up.total_len = 0;
    g_up.crc = 0;
}

/* --- base64 decode (strict, 4-in/3-out blocks) --------------------------- */
static int b64val(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return 26 + (c - 'a');
    if (c >= '0' && c <= '9') return 52 + (c - '0');
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/* Decode a NUL-terminated base64 string into dst. Returns #bytes written or -1. */
static int b64_decode(const char *src, uint8_t *dst, int dstmax) {
    int out = 0;
    uint32_t v = 0;
    int n = 0;
    for (; *src; src++) {
        if (*src == '=') break;
        if (*src == '\r' || *src == '\n' || *src == ' ') continue;
        int d = b64val((int)(unsigned char)*src);
        if (d < 0) return -1;
        v = (v << 6) | (uint32_t)d;
        n++;
        if (n == 4) {
            if (out + 3 > dstmax) return -1;
            dst[out++] = (uint8_t)((v >> 16) & 0xFF);
            dst[out++] = (uint8_t)((v >> 8) & 0xFF);
            dst[out++] = (uint8_t)(v & 0xFF);
            v = 0; n = 0;
        }
    }
    if (n == 2) {
        if (out + 1 > dstmax) return -1;
        dst[out++] = (uint8_t)((v >> 10) & 0xFF);
    } else if (n == 3) {
        if (out + 2 > dstmax) return -1;
        dst[out++] = (uint8_t)((v >> 16) & 0xFF);
        dst[out++] = (uint8_t)((v >> 8) & 0xFF);
    } else if (n != 0) {
        return -1;   /* malformed */
    }
    return out;
}

/* --- uploads -------------------------------------------------------------- */
static void up_start(uint32_t offset, uint32_t total) {
    if (offset < EXT_OFF_IMAGE_STORE) {
        printf("ERR offset-must-be->=0x%06X\r\n", EXT_OFF_IMAGE_STORE);
        return;
    }
    if (offset + total > EXT_FLASH_SIZE) {
        printf("ERR overflow\r\n");
        return;
    }
    /* Erase the covering sector range up front so page programs never hit a
     * still-set page — this is the protocol-level guard against the same
     * NOR erase-before-write hazard fixed in shadow_map_save. */
    ext_flash_erase_range(offset, total);

    g_up.active = true;
    g_up.dest_offset = offset;
    g_up.cursor = offset;
    g_up.total_len = total;
    g_up.received = 0;
    g_up.crc = 0;
    printf("OK upload-started %lu\r\n", (unsigned long)total);
}

static void up_chunk(const char *b64) {
    if (!g_up.active) {
        printf("ERR no-upload\r\n");
        return;
    }
    int n = b64_decode(b64, g_upbuf, CHUNK_RAW);
    if (n < 0) {
        printf("ERR b64\r\n");
        return;
    }
    if (g_up.received + (uint32_t)n > g_up.total_len) {
        printf("ERR overflow\r\n");
        binary_reset();
        return;
    }
    ext_flash_write_buf(g_up.cursor, g_upbuf, (uint32_t)n);
    g_up.crc = crc32_update(g_up.crc, g_upbuf, (uint32_t)n);
    g_up.cursor += (uint32_t)n;
    g_up.received += (uint32_t)n;
    printf("OK %lu\r\n", (unsigned long)g_up.received);
}

static void up_done(uint32_t expected_crc) {
    if (!g_up.active) {
        printf("ERR no-upload\r\n");
        return;
    }
    bool ok_len = (g_up.received == g_up.total_len);
    bool ok_crc = (g_up.crc == expected_crc);
    g_up.active = false;
    if (!ok_len) {
        printf("ERR length %lu/%lu\r\n",
               (unsigned long)g_up.received, (unsigned long)g_up.total_len);
        return;
    }
    if (!ok_crc) {
        printf("ERR crc 0x%08lX != 0x%08lX\r\n",
               (unsigned long)g_up.crc, (unsigned long)expected_crc);
        return;
    }

    /* ── Phase A: signature verification after transport checks ──────────
     * If the uploaded data has a valid signed-image header at the dest
     * offset, verify it. On failure, erase the uploaded bytes — the host
     * must re-upload a properly signed image. */
    if (g_up.total_len >= VBFC_IMAGE_HDR_SIZE) {
        if (!image_check_verify_header(g_up.dest_offset, NULL)) {
            /* erase the uploaded range (incl. header) */
            ext_flash_erase_range(g_up.dest_offset,
                                  g_up.total_len + VBFC_IMAGE_HDR_SIZE);
            printf("ERR signature\r\n");
            return;
        }

        /* anti-rollback: read the just-verified header and check version */
        vbfc_image_header_t hdr;
        ext_flash_read_buf(g_up.dest_offset, (uint8_t *)&hdr, sizeof(hdr));
        if (hdr.image_version < image_check_accepted_version()) {
            ext_flash_erase_range(g_up.dest_offset,
                                  g_up.total_len + VBFC_IMAGE_HDR_SIZE);
            printf("ERR rollback\r\n");
            return;
        }
        image_check_accept_version(hdr.image_version);
        image_check_populate_cache(g_up.dest_offset);
    }

    printf("OK upload-done\r\n");
}

/* --- dumps (device -> host) ---------------------------------------------- */
static void dump_range(const uint8_t *src_name, uint32_t offset, uint32_t len,
                       void (*read)(uint32_t, uint8_t *, uint32_t)) {
    uint32_t crc = 0;
    uint32_t remaining = len;
    uint32_t addr = offset;
    printf("DUMP %s START 0x%06lX %lu\r\n", src_name,
           (unsigned long)offset, (unsigned long)len);
    while (remaining > 0) {
        uint32_t n = remaining < CHUNK_RAW ? remaining : CHUNK_RAW;
        read(addr, g_upbuf, n);
        size_t k = b64_encode(g_upbuf, n, g_b64buf);
        g_b64buf[k] = '\0';
        crc = crc32_update(crc, g_upbuf, n);
        printf("DUMP CHUNK %s\r\n", g_b64buf);
        addr += n;
        remaining -= n;
    }
    printf("DUMP DONE 0x%08lX\r\n", (unsigned long)crc);
}

static void read_ext(uint32_t off, uint8_t *buf, uint32_t n) {
    ext_flash_read_buf(off, buf, n);
}
static void read_orig(uint32_t off, uint8_t *buf, uint32_t n) {
    orig_flash_read_buf(off, buf, n);
}

void binary_dump_ext(uint32_t offset, uint32_t len) {
    if (offset + len > EXT_FLASH_SIZE) {
        printf("ERR overflow\r\n");
        return;
    }
    dump_range((const uint8_t *)"EXT", offset, len, read_ext);
}

void binary_dump_orig(uint32_t offset, uint32_t len) {
    /* The original chip sits behind the MCU's own SPI master and is typically
     * <= 16 MB; bound the read the same way EXT is bounded so a stray/oversized
     * request can't run off the end of the address space and hang the bus.
     * Reading the original chip through the MCU is slow and completes its own
     * CS pulse per call; the caller still needs the real chip size to get
     * meaningful bytes — but we at least can't overflow the 24-bit address
     * window the orig chip would respond in. */
    if (offset + len > EXT_FLASH_SIZE) {
        printf("ERR overflow\r\n");
        return;
    }
    dump_range((const uint8_t *)"ORIG", offset, len, read_orig);
}

/* --- line dispatch -------------------------------------------------------- */
bool binary_feed(const char *line) {
    if (strncmp(line, "ULOAD ", 6) == 0) {
        const char *rest = line + 6;
        if (strncmp(rest, "START ", 6) == 0) {
            unsigned long off = 0, total = 0;
            if (sscanf(rest + 6, "%lu %lu", &off, &total) == 2) {
                up_start((uint32_t)off, (uint32_t)total);
            } else {
                printf("ERR parse\r\n");
            }
            return true;
        }
        if (strncmp(rest, "CHUNK ", 6) == 0) {
            up_chunk(rest + 6);
            return true;
        }
        if (strncmp(rest, "DONE ", 5) == 0) {
            unsigned long crc = 0;
            if (sscanf(rest + 5, "%lu", &crc) == 1 ||
                sscanf(rest + 5, "0x%lX", &crc) == 1) {
                up_done((uint32_t)crc);
            } else {
                printf("ERR parse\r\n");
            }
            return true;
        }
        return true;
    }

    if (strncmp(line, "DUMP ", 5) == 0) {
        const char *rest = line + 5;
        if (strncmp(rest, "EXT ", 4) == 0) {
            unsigned long off = 0, len = 0;
            if (sscanf(rest + 4, "%lu %lu", &off, &len) == 2) {
                binary_dump_ext((uint32_t)off, (uint32_t)len);
            } else {
                printf("ERR parse\r\n");
            }
            return true;
        }
        if (strncmp(rest, "ORIG ", 5) == 0) {
            unsigned long off = 0, len = 0;
            if (sscanf(rest + 5, "%lu %lu", &off, &len) == 2) {
                binary_dump_orig((uint32_t)off, (uint32_t)len);
            } else {
                printf("ERR parse\r\n");
            }
            return true;
        }
        if (strncmp(rest, "SNIFF", 5) == 0) {
            sniffer_dump();
            return true;
        }
        return true;
    }
    return false;
}

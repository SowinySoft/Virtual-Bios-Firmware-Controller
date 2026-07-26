/*
 * USB CDC command service.
 *
 * Two layers:
 *   - vbfc_cli text protocol (line-oriented, ASCII): mode/map/patch/flash/
 *     sniff/status/version/reboot.
 *   - binary framing (ULOAD/DUMP) delegated to proto_binary.c for image
 *     upload/backup/read and sniff dumps.
 *
 * Replies use \r\n so the host readline() can stream them uniformly. The
 * terminal token for a command's reply set is "OK" or "ERR <why>".
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "pico/stdlib.h"
#include "hardware/watchdog.h"

#include "usb_service.h"
#include "shadow_map.h"
#include "patch_table.h"
#include "proto_binary.h"
#include "sniffer.h"
#include "orig_flash.h"
#include "ext_flash.h"
#include "safety.h"

#define LINE_BUF_SIZE 2048   /* big enough for a 1024-char base64 CHUNK line */
#define FW_VERSION "1.1.0"

static char line_buf[LINE_BUF_SIZE];
static uint32_t line_len = 0;

static void ok(void)            { printf("OK\r\n"); }
static void err(const char *m)  { printf("ERR %s\r\n", m); }

/* ---- helpers ------------------------------------------------------------- */
static const char *mode_name(vbfc_mode_t m) {
    switch (m) {
    case VBFC_MODE_SHADOW:      return "shadow";
    case VBFC_MODE_HOTPATCH:   return "hotpatch";
    case VBFC_MODE_PASSTHROUGH:return "pass-through";
    default:                    return "unknown";
    }
}

static vbfc_mode_t mode_parse(const char *s) {
    if (strcmp(s, "shadow") == 0)        return VBFC_MODE_SHADOW;
    if (strcmp(s, "hotpatch") == 0)     return VBFC_MODE_HOTPATCH;
    if (strcmp(s, "pass-through") == 0) return VBFC_MODE_PASSTHROUGH;
    return (vbfc_mode_t)0xFF;
}

/* parse a hex-or-decimal integer token into a uint32; returns success */
static bool parse_num(const char *s, uint32_t *out) {
    if (!s || !*s) return false;
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 0);   /* 0 = auto base (handles 0x) */
    if (end == s) return false;
    *out = (uint32_t)v;
    return true;
}

/* ---- map commands -------------------------------------------------------- */
static void cmd_get_map(void) {
    const vbfc_shadow_map_t *map = shadow_map_get();
    for (uint8_t i = 0; i < map->entry_count; i++) {
        const vbfc_map_entry_t *e = &map->entries[i];
        printf("MAP %u 0x%08lX %lu %s 0x%lX\r\n", i,
               (unsigned long)e->start_addr, (unsigned long)e->size,
               e->source == VBFC_SOURCE_EXT ? "ext" : "orig",
               (unsigned long)e->ext_offset);
    }
    ok();
}

static void cmd_map_add(const char *args) {
    vbfc_map_entry_t entry = {0};
    char source[16] = {0};
    unsigned long start = 0, size = 0, ext_off = 0;
    int got = sscanf(args, "%lu %lu %15s %lu",
                     &start, &size, source, &ext_off);
    if (got < 3) { err("parse"); return; }
    entry.start_addr = (uint32_t)start;
    entry.size = (uint32_t)size;
    entry.ext_offset = (uint32_t)ext_off;
    entry.source = (strcmp(source, "ext") == 0) ? VBFC_SOURCE_EXT
                                                : VBFC_SOURCE_ORIG;
    shadow_map_add_entry(&entry) ? ok() : err("add");
}

static void cmd_map_remove(const char *args) {
    uint32_t idx;
    if (!parse_num(args, &idx)) { err("parse"); return; }
    shadow_map_remove_entry((uint8_t)idx) ? ok() : err("range");
}

static void cmd_map_clear(void) {
    shadow_map_clear();
    ok();
}

/* ---- mode ---------------------------------------------------------------- */
static void cmd_get_mode(void) {
    printf("MODE %s\r\n", mode_name(shadow_map_get_mode()));
}
static void cmd_set_mode(const char *args) {
    vbfc_mode_t m = mode_parse(args);
    if (m == (vbfc_mode_t)0xFF) { err("mode"); return; }
    shadow_map_set_mode(m) ? ok() : err("save");
}

/* ---- patch --------------------------------------------------------------- */
static void cmd_patch_add(const char *args) {
    /* PATCH ADD <addr> <orig> <new>  (orig = 0xFF means wildcard) */
    uint32_t a, o, n;
    if (sscanf(args, "%li %li %li", (long *)&a, (long *)&o, (long *)&n) != 3 &&
        sscanf(args, "0x%lX %li %li", &a, &o, &n) != 3) {
        err("parse"); return;
    }
    vbfc_patch_entry_t e = {
        .addr = (uint32_t)a,
        .orig_byte = (uint8_t)o,
        .new_byte = (uint8_t)n,
        .enabled = 1,
    };
    patch_table_add(&e) ? ok() : err("full");
}

static void cmd_patch_list(void) {
    const vbfc_patch_table_t *t = patch_table_get();
    for (uint8_t i = 0; i < t->count; i++) {
        const vbfc_patch_entry_t *e = &t->entries[i];
        printf("PATCH %u 0x%08lX 0x%02X 0x%02X %s\r\n", i,
               (unsigned long)e->addr, e->orig_byte, e->new_byte,
               e->enabled ? "on" : "off");
    }
    ok();
}

static void cmd_patch_remove(const char *args) {
    uint32_t idx;
    if (!parse_num(args, &idx)) { err("parse"); return; }
    patch_table_remove((uint8_t)idx) ? ok() : err("range");
}

static void cmd_patch_clear(void) {
    patch_table_clear();
    ok();
}

/* ---- flash --------------------------------------------------------------- */
static void cmd_flash_erase(const char *args) {
    /* FLASH ERASE <offset> <len>  (ext flash; sectors rounded) */
    uint32_t off, len;
    if (sscanf(args, "%lu %lu", (unsigned long *)&off, (unsigned long *)&len) != 2 &&
        sscanf(args, "0x%lX %lu", &off, &len) != 2) {
        err("parse"); return;
    }
    if (off + len > EXT_FLASH_SIZE) { err("overflow"); return; }
    if (off < EXT_OFF_IMAGE_STORE)  { err("reserved"); return; }
    ext_flash_erase_range(off, len);
    ok();
}

static void cmd_flash_read(const char *args) {
    /* FLASH READ <ext|orig> <offset> <len> — streams via DUMP framing */
    char which[8] = {0};
    uint32_t off, len;
    if (sscanf(args, "%7s %lu %lu", which,
               (unsigned long *)&off, (unsigned long *)&len) != 3 &&
        sscanf(args, "%7s 0x%lX %lu", which, &off, &len) != 3) {
        err("parse"); return;
    }
    if (strcmp(which, "ext") == 0) {
        binary_dump_ext(off, len);
    } else if (strcmp(which, "orig") == 0) {
        binary_dump_orig(off, len);
    } else {
        err("which");
    }
}

static void cmd_flash_backup(const char *args) {
    /* FLASH BACKUP [<len>] — dump the original chip in one go. The ORIG chip
     * is almost always 8 MB (the classic SPI BIOS size); 16 MB boards exist
     * but the host should pass an explicit length for those. The default keeps
     * us from reading 16 MB off an 8 MB chip and hanging the bus. */
    uint32_t len = (8u * 1024u * 1024u);
    if (args && *args) parse_num(args, &len);
    binary_dump_orig(0, len);
}

static void cmd_flash_restore(const char *args) {
    /* FLASH RESTORE <offset> <len> — host then issues ULOAD START/CHUNK/DONE.
     * We just echo the plan; bulk bytes come via the binary channel. */
    uint32_t off, len;
    if (sscanf(args, "%lu %lu", (unsigned long *)&off, (unsigned long *)&len) != 2 &&
        sscanf(args, "0x%lX %lu", &off, &len) != 2) {
        err("parse"); return;
    }
    printf("RESTORE PLAN orig 0x%06lX %lu — send ULOAD START %lu %lu next\r\n",
           (unsigned long)off, (unsigned long)len,
           (unsigned long)off, (unsigned long)len);
    ok();
}

/* ---- sniff --------------------------------------------------------------- */
static void cmd_sniff(const char *sub) {
    if (strcmp(sub, "start") == 0) {
        if (safety_bypass_active()) { err("bypass"); return; }
        sniffer_start(); ok();
    } else if (strcmp(sub, "stop") == 0) {
        sniffer_stop(); ok();
    } else if (strcmp(sub, "status") == 0) {
        printf("SNIFF %s events %lu / %lu\r\n",
               sniffer_is_active() ? "on" : "off",
               (unsigned long)sniffer_count(),
               (unsigned long)sniffer_capacity());
        ok();
    } else if (strcmp(sub, "dump") == 0) {
        sniffer_dump();
    } else {
        err("sub");
    }
}

/* ---- status / version / reboot ------------------------------------------- */
static void cmd_status(void) {
    uint8_t id[3] = {0};
    orig_flash_read_jedec_id(id);
    printf("STATUS mode %s\r\n", mode_name(shadow_map_get_mode()));
    printf("STATUS vox-bypass %s\r\n", safety_bypass_active() ? "on" : "off");
    printf("STATUS orig-jedec 0x%02X 0x%02X 0x%02X\r\n", id[0], id[1], id[2]);
    ext_flash_read_jedec_id(id);
    printf("STATUS ext-jedec  0x%02X 0x%02X 0x%02X\r\n", id[0], id[1], id[2]);
    const vbfc_shadow_map_t *m = shadow_map_get();
    printf("STATUS map-entries %u\r\n", m->entry_count);
    printf("STATUS patch-entries %u\r\n", patch_table_get()->count);
    printf("STATUS sniff %s\r\n", sniffer_is_active() ? "on" : "off");
    ok();
}

static void cmd_version(void) {
    printf("VERSION firmware %s\r\n", FW_VERSION);
    printf("VERSION proto %s\r\n", "text+b64-v1");
    ok();
}

static void cmd_reboot(void) {
    printf("OK rebooting\r\n");
    sleep_ms(20);
    watchdog_reboot(0, 0);
}

static void cmd_factory_reset(void) {
    shadow_map_factory_reset();
    patch_table_reset();
    sniffer_clear();
    binary_reset();
    ok();
}

/* ---- dispatch ----------------------------------------------------------- */
static void handle_line(char *line) {
    while (*line && isspace((unsigned char)*line)) line++;
    if (*line == '\0') return;

    /* Binary-framed commands bypass the text dispatcher but share the line. */
    if (binary_feed(line)) return;

    if (strcmp(line, "PING") == 0)            { printf("PONG vbfc-%s\r\n", FW_VERSION); return; }
    if (strcmp(line, "VERSION") == 0)        { cmd_version(); return; }
    if (strcmp(line, "STATUS") == 0)         { cmd_status(); return; }
    if (strcmp(line, "REBOOT") == 0)         { cmd_reboot(); return; }
    if (strcmp(line, "FACTORY RESET") == 0)  { cmd_factory_reset(); return; }

    if (strcmp(line, "GET MODE") == 0)       { cmd_get_mode(); return; }
    if (strncmp(line, "SET MODE ", 9) == 0)  { cmd_set_mode(line + 9); return; }

    if (strcmp(line, "GET MAP") == 0)        { cmd_get_map(); return; }
    if (strncmp(line, "MAP ADD ", 8) == 0)   { cmd_map_add(line + 8); return; }
    if (strncmp(line, "MAP REMOVE ", 11) == 0){ cmd_map_remove(line + 11); return; }
    if (strcmp(line, "MAP CLEAR") == 0)      { cmd_map_clear(); return; }

    if (strncmp(line, "PATCH ADD ", 10) == 0){ cmd_patch_add(line + 10); return; }
    if (strcmp(line, "PATCH LIST") == 0)     { cmd_patch_list(); return; }
    if (strncmp(line, "PATCH REMOVE ", 13) == 0){ cmd_patch_remove(line + 13); return; }
    if (strcmp(line, "PATCH CLEAR") == 0)     { cmd_patch_clear(); return; }

    if (strncmp(line, "FLASH ERASE ", 12) == 0){ cmd_flash_erase(line + 12); return; }
    if (strncmp(line, "FLASH READ ", 11) == 0){ cmd_flash_read(line + 11); return; }
    if (strncmp(line, "FLASH BACKUP", 11) == 0){ cmd_flash_backup(line[11] ? line + 12 : NULL); return; }
    if (strncmp(line, "FLASH RESTORE ", 14) == 0){ cmd_flash_restore(line + 14); return; }

    if (strncmp(line, "SNIFF ", 6) == 0)     { cmd_sniff(line + 6); return; }

    err("unknown");
}

void usb_service_init(void) {
    line_len = 0;
    binary_init();
}

void usb_service_poll(void) {
    int c;
    while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (c == '\r') continue;
        if (c == '\n') {
            line_buf[line_len] = '\0';
            handle_line(line_buf);
            line_len = 0;
            continue;
        }
        if (line_len < LINE_BUF_SIZE - 1) {
            line_buf[line_len++] = (char)c;
        }
    }
}

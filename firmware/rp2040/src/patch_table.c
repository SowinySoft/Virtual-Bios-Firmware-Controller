#include <string.h>

#include "vbfc_crc.h"

#include "patch_table.h"
#include "ext_flash.h"

static vbfc_patch_table_t g_table;

static uint32_t pt_compute_crc(const vbfc_patch_table_t *t) {
    /* CRC over the entries array only; count defines how many are live. */
    return crc32_full((const uint8_t *)t->entries,
                      (size_t)t->count * sizeof(vbfc_patch_entry_t));
}

static void pt_set_defaults(vbfc_patch_table_t *t) {
    memset(t, 0, sizeof(*t));
    t->magic = VBFC_PATCH_MAGIC;
    t->version = 1;
    t->count = 0;
    t->crc32 = pt_compute_crc(t);
}

void patch_table_init(void) {
    pt_set_defaults(&g_table);
}

bool patch_table_validate(const vbfc_patch_table_t *t) {
    if (t->magic != VBFC_PATCH_MAGIC) return false;
    if (t->count > VBFC_PATCH_MAX) return false;
    if (pt_compute_crc(t) != t->crc32) return false;
    return true;
}

bool patch_table_load(void) {
    vbfc_patch_table_t tmp;
    ext_flash_read_buf(EXT_OFF_PATCH_META, (uint8_t *)&tmp, sizeof(tmp));
    if (!patch_table_validate(&tmp)) return false;
    g_table = tmp;
    return true;
}

bool patch_table_save(void) {
    g_table.crc32 = pt_compute_crc(&g_table);
    if (!patch_table_validate(&g_table)) return false;
    /* Mirror into ext-flash metadata sector; NOR requires erase-before-write
     * so a re-save with cleared 1->0 bits stays consistent. */
    ext_flash_erase_range(EXT_OFF_PATCH_META, sizeof(g_table));
    ext_flash_write_buf(EXT_OFF_PATCH_META, (const uint8_t *)&g_table, sizeof(g_table));
    return true;
}

void patch_table_reset(void) {
    pt_set_defaults(&g_table);
    patch_table_cache_invalidate();
    patch_table_save();
}

bool patch_table_add(const vbfc_patch_entry_t *e) {
    if (g_table.count >= VBFC_PATCH_MAX) return false;
    /* De-dup: replace an existing entry at the same address instead of growing. */
    for (uint8_t i = 0; i < g_table.count; i++) {
        if (g_table.entries[i].addr == e->addr) {
            g_table.entries[i] = *e;
            patch_table_cache_invalidate();
            return patch_table_save();
        }
    }
    g_table.entries[g_table.count] = *e;
    g_table.entries[g_table.count].enabled = e->enabled ? 1 : 0;
    g_table.count++;
    patch_table_cache_invalidate();
    return patch_table_save();
}

bool patch_table_remove(uint8_t index) {
    if (index >= g_table.count) return false;
    /* Swap-remove to keep the live region contiguous for CRC/lookup. */
    g_table.entries[index] = g_table.entries[g_table.count - 1];
    memset(&g_table.entries[g_table.count - 1], 0, sizeof(vbfc_patch_entry_t));
    g_table.count--;
    patch_table_cache_invalidate();
    return patch_table_save();
}

void patch_table_clear(void) {
    g_table.count = 0;
    memset(g_table.entries, 0, sizeof(g_table.entries));
    patch_table_cache_invalidate();
    patch_table_save();
}

const vbfc_patch_table_t *patch_table_get(void) {
    return &g_table;
}

/* Tiny direct-mapped address cache — the most-frequently matched address
 * hits in O(1) instead of scanning up to 64 entries per byte. Resets on
 * table modifications. */
static struct {
    uint32_t addr;
    uint8_t  new_byte;
    bool     valid;
} g_match_cache;

bool patch_table_match(uint32_t addr, uint8_t orig_byte, uint8_t *out) {
    /* Check cache first */
    if (g_match_cache.valid && g_match_cache.addr == addr) {
        if (out) *out = g_match_cache.new_byte;
        return true;
    }
    for (uint8_t i = 0; i < g_table.count; i++) {
        const vbfc_patch_entry_t *e = &g_table.entries[i];
        if (!e->enabled || e->addr != addr) continue;
        if (e->orig_byte != 0xFF && e->orig_byte != orig_byte) continue;
        if (out) *out = e->new_byte;
        /* Cache this hit */
        g_match_cache.addr = addr;
        g_match_cache.new_byte = e->new_byte;
        g_match_cache.valid = true;
        return true;
    }
    return false;
}

void patch_table_cache_invalidate(void) {
    g_match_cache.valid = false;
}

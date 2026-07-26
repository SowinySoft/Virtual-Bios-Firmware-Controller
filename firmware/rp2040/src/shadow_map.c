#include <string.h>

#include "vbfc_crc.h"

#include "shadow_map.h"
#include "config_store.h"
#include "ext_flash.h"

static vbfc_shadow_map_t g_map;

static uint32_t map_compute_crc(const vbfc_shadow_map_t *map) {
    return crc32_full((const uint8_t *)map->entries,
                      (size_t)map->entry_count * sizeof(vbfc_map_entry_t));
}

static void map_set_defaults(vbfc_shadow_map_t *map) {
    memset(map, 0, sizeof(*map));
    map->magic = VBFC_MAGIC;
    map->version = VBFC_MAP_VERSION;
    map->mode = VBFC_MODE_PASSTHROUGH;
    map->entry_count = 1;
    map->entries[0].start_addr = 0x00FF0000u;
    map->entries[0].size = 65536u;
    map->entries[0].source = VBFC_SOURCE_EXT;
    map->entries[0].ext_offset = 0;
    map->crc32 = map_compute_crc(map);
}

void shadow_map_init(void) {
    map_set_defaults(&g_map);
}

bool shadow_map_validate(const vbfc_shadow_map_t *map) {
    if (map->magic != VBFC_MAGIC) return false;
    if (map->version != VBFC_MAP_VERSION) return false;
    if (map->entry_count > VBFC_MAX_ENTRIES) return false;
    if (map_compute_crc(map) != map->crc32) return false;

    for (uint8_t i = 0; i < map->entry_count; i++) {
        const vbfc_map_entry_t *e = &map->entries[i];
        if (e->size < 4096 || (e->size & (e->size - 1)) != 0) return false;
        if (e->start_addr % e->size != 0) return false;
        if (e->ext_offset + e->size > EXT_FLASH_SIZE) return false;

        for (uint8_t j = i + 1; j < map->entry_count; j++) {
            const vbfc_map_entry_t *o = &map->entries[j];
            uint32_t a0 = e->start_addr, a1 = a0 + e->size;
            uint32_t b0 = o->start_addr, b1 = b0 + o->size;
            if (a0 < b1 && b0 < a1) return false;
        }
    }
    return true;
}

bool shadow_map_load(void) {
    vbfc_shadow_map_t tmp;
    if (!config_store_read_map(&tmp)) return false;
    if (!shadow_map_validate(&tmp)) return false;
    g_map = tmp;
    return true;
}

bool shadow_map_save(void) {
    g_map.crc32 = map_compute_crc(&g_map);
    if (!shadow_map_validate(&g_map)) return false;
    if (!config_store_write_map(&g_map)) return false;
    /* Mirror the validated map into the ext-flash metadata sector. NOR flash
     * requires erase-before-write, so erase the 4 KB sector that owns
     * EXT_OFF_MAP_META before re-programming it — otherwise re-saves would
     * leave stale bits set and corrupt the block. */
    ext_flash_erase_range(EXT_OFF_MAP_META, sizeof(g_map));
    ext_flash_write_buf(EXT_OFF_MAP_META, (const uint8_t *)&g_map, sizeof(g_map));
    return true;
}

void shadow_map_factory_reset(void) {
    map_set_defaults(&g_map);
    shadow_map_save();
}

vbfc_mode_t shadow_map_get_mode(void) {
    return g_map.mode;
}

bool shadow_map_set_mode(vbfc_mode_t mode) {
    g_map.mode = mode;
    return shadow_map_save();
}

vbfc_source_t shadow_map_lookup(uint32_t addr, vbfc_map_entry_t *out) {
    /* EXT redirection is honored in both SHADOW and HOTPATCH modes. HOTPATCH
     * adds ORIG-byte patching on top (the arbiter reads the real byte then
     * substitutes via the patch table) — but a mapped EXT region must still
     * be served from EXT, otherwise the EXT read/write branches the arbiter
     * already implements would be dead code under HOTPATCH. PASSTHROUGH (and
     * the FPGA-only DEV_EMULATE) leave every address on ORIG. */
    if (g_map.mode != VBFC_MODE_SHADOW && g_map.mode != VBFC_MODE_HOTPATCH) {
        return VBFC_SOURCE_ORIG;
    }

    for (uint8_t i = 0; i < g_map.entry_count; i++) {
        const vbfc_map_entry_t *e = &g_map.entries[i];
        if (addr >= e->start_addr && addr < e->start_addr + e->size) {
            if (out) *out = *e;
            return e->source;
        }
    }
    return VBFC_SOURCE_ORIG;
}

bool shadow_map_add_entry(const vbfc_map_entry_t *entry) {
    if (g_map.entry_count >= VBFC_MAX_ENTRIES) return false;
    g_map.entries[g_map.entry_count++] = *entry;
    return shadow_map_save();
}

bool shadow_map_remove_entry(uint8_t index) {
    if (g_map.entry_count == 0 || index >= g_map.entry_count) return false;
    /* Swap-remove keeps the live prefix contiguous. */
    g_map.entries[index] = g_map.entries[g_map.entry_count - 1];
    memset(&g_map.entries[g_map.entry_count - 1], 0, sizeof(vbfc_map_entry_t));
    g_map.entry_count--;
    return shadow_map_save();
}

void shadow_map_clear(void) {
    g_map.entry_count = 0;
    memset(g_map.entries, 0, sizeof(g_map.entries));
    shadow_map_save();
}

const vbfc_shadow_map_t *shadow_map_get(void) {
    return &g_map;
}

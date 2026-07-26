#ifndef VBFC_SHADOW_MAP_H
#define VBFC_SHADOW_MAP_H

#include <stdint.h>
#include <stdbool.h>

#define VBFC_MAGIC       0x56424643u  /* "VBFC" */
#define VBFC_MAP_VERSION 0x01
#define VBFC_MAX_ENTRIES 4

typedef enum {
    VBFC_SOURCE_ORIG = 0,
    VBFC_SOURCE_EXT  = 1,
    VBFC_SOURCE_OVERLAY = 2,  /* reserved */
} vbfc_source_t;

typedef enum {
    VBFC_MODE_PASSTHROUGH = 0x00,
    VBFC_MODE_SHADOW      = 0x01,
    VBFC_MODE_DEV_EMULATE = 0x02,  /* FPGA tier */
    VBFC_MODE_HOTPATCH    = 0x03,  /* ORIG bytes intercepted + patched in-flight */
} vbfc_mode_t;

typedef struct {
    uint32_t start_addr;
    uint32_t size;
    vbfc_source_t source;
    uint32_t ext_offset;
} vbfc_map_entry_t;

typedef struct {
    uint32_t magic;
    uint8_t  version;
    uint8_t  entry_count;
    vbfc_mode_t mode;
    uint8_t  flags;
    uint32_t crc32;
    vbfc_map_entry_t entries[VBFC_MAX_ENTRIES];
} vbfc_shadow_map_t;

void shadow_map_init(void);
bool shadow_map_load(void);
bool shadow_map_save(void);
void shadow_map_factory_reset(void);

vbfc_mode_t shadow_map_get_mode(void);
bool shadow_map_set_mode(vbfc_mode_t mode);

/* Returns source for byte address; defaults to ORIG if unmapped. */
vbfc_source_t shadow_map_lookup(uint32_t addr, vbfc_map_entry_t *out);

bool shadow_map_add_entry(const vbfc_map_entry_t *entry);
bool shadow_map_remove_entry(uint8_t index);
void shadow_map_clear(void);
bool shadow_map_validate(const vbfc_shadow_map_t *map);

const vbfc_shadow_map_t *shadow_map_get(void);

#endif /* VBFC_SHADOW_MAP_H */

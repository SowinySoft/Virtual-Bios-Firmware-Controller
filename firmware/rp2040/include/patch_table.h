#ifndef VBFC_PATCH_TABLE_H
#define VBFC_PATCH_TABLE_H

#include <stdint.h>
#include <stdbool.h>

#define VBFC_PATCH_MAX 64

typedef struct {
    uint32_t addr;       /* target byte address in flash space */
    uint8_t  orig_byte;  /* expected original value; 0xFF = wildcard (no verify) */
    uint8_t  new_byte;   /* replacement value */
    uint8_t  enabled;    /* 0 = disabled, !0 = enabled */
    uint8_t  _pad;
} vbfc_patch_entry_t;

#define VBFC_PATCH_MAGIC 0x56425F50u  /* "VB_P" */

typedef struct {
    uint32_t magic;
    uint8_t  version;
    uint8_t  count;             /* number of enabled/defined entries */
    uint16_t _pad;
    uint32_t crc32;
    vbfc_patch_entry_t entries[VBFC_PATCH_MAX];
} vbfc_patch_table_t;

void patch_table_init(void);
bool patch_table_load(void);
bool patch_table_save(void);
void patch_table_reset(void);

bool patch_table_add(const vbfc_patch_entry_t *e);
bool patch_table_remove(uint8_t index);
void patch_table_clear(void);
const vbfc_patch_table_t *patch_table_get(void);

/* Look up an override for `addr`. Returns true and fills *out if the byte
 * should be substituted; false otherwise. Substitutes happen in the
 * arbiter's read path before the byte reaches the motherboard. */
bool patch_table_match(uint32_t addr, uint8_t orig_byte, uint8_t *out);

/* Invalidate the hot-match address cache. Called on table mutation. */
void patch_table_cache_invalidate(void);

bool patch_table_validate(const vbfc_patch_table_t *t);

#endif /* VBFC_PATCH_TABLE_H */

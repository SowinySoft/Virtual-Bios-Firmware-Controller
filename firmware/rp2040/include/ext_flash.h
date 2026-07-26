#ifndef VBFC_EXT_FLASH_H
#define VBFC_EXT_FLASH_H

#include <stdint.h>

#define EXT_FLASH_SIZE       (16u * 1024u * 1024u)   /* W25Q128 = 16 MB */
#define EXT_FLASH_PAGE_SIZE  256u                      /* program page */
#define EXT_FLASH_SECTOR     4096u                     /* erase unit */

/*
 * High-performance image store layout on the W25Q128:
 *
 *   0x000000  Sector 0  ── shadow-map metadata (config mirror)  [4 KB]
 *   0x001000  Sector 1  ── hot-patch table mirror              [4 KB]
 *   0x002000  Sector 2+ ── firmware image store (no FS)
 *                          Images stored back-to-back from here;
 *                          host CLI ranges them by byte offset.
 *
 * Tagged headers at the head of each metadata sector let us detect a
 * valid block cheaply and survive partial writes.
 */
#define EXT_OFF_MAP_META     0x000000u
#define EXT_OFF_PATCH_META   0x001000u
#define EXT_OFF_IMAGE_STORE  0x002000u

void ext_flash_init(void);

/* Bulk read — len can be large; caller owns buf. */
void ext_flash_read_buf(uint32_t offset, uint8_t *buf, uint32_t len);

/* Bulk write — handles 256-byte page boundaries internally. Caller MUST
 * have erased the destination sectors first (NOR: erase-before-write). */
void ext_flash_write_buf(uint32_t offset, const uint8_t *data, uint32_t len);

/* Erase one 4 KB sector containing `offset`. Blocks ~ms for the erase. */
void ext_flash_sector_erase(uint32_t offset);

/* Erase `len` bytes worth of sectors starting at `offset` (rounds up). */
void ext_flash_erase_range(uint32_t offset, uint32_t len);

/* JEDEC id of the external chip (3 bytes), for image-store sanity checks. */
void ext_flash_read_jedec_id(uint8_t id[3]);

#endif /* VBFC_EXT_FLASH_H */

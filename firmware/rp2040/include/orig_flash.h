#ifndef VBFC_ORIG_FLASH_H
#define VBFC_ORIG_FLASH_H

#include <stdint.h>

void orig_flash_init(void);

uint8_t orig_flash_read_byte(uint32_t addr);
void orig_flash_read_buf(uint32_t addr, uint8_t *buf, uint32_t len);

void orig_flash_page_program(uint32_t addr, const uint8_t *data, uint32_t len);
void orig_flash_sector_erase(uint32_t addr);

void orig_flash_read_jedec_id(uint8_t id[3]);

void orig_flash_sleep(void);
void orig_flash_wake(void);

#endif /* VBFC_ORIG_FLASH_H */

#include <string.h>

#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"

#include "pins.h"
#include "shadow_map.h"

#define CONFIG_EEPROM_ADDR 0x50
#define EEPROM_PAGE_SIZE    8      /* 24C02 = 8-byte page; writes crossing a page wrap */
#define EEPROM_WRITE_MS    5       /* tWR: settle after each page write */

/*
 * Layout: the shadow map struct lives at EEPROM byte offset 0. We split all
 * accesses into < EEPROM_PAGE_SIZE chunks aligned to page boundaries so the
 * 24C02's internal page wrap never folds distinct bytes onto a wrong cell.
 * The struct currently fits (~80 bytes); if it ever grows past 256 bytes we
 * must switch parts (24C02 = 256B total) — guarded by the static_assert.
 */

static_assert(sizeof(vbfc_shadow_map_t) <= 256,
               "shadow map struct exceeds 24C02 capacity");

static void bus_init(void) {
    static bool inited = false;
    if (inited) return;
    i2c_init(I2C_PORT, 100 * 1000);
    gpio_set_function(PIN_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_I2C_SDA);
    gpio_pull_up(PIN_I2C_SCL);
    inited = true;
}

/* Random-read: a zero-length write sets the address pointer, then we read. */
static bool eeprom_read_chunk(uint16_t addr, uint8_t *buf, uint8_t len) {
    uint8_t a = (uint8_t)addr;          /* 24C02: 8-bit word address */
    int r = i2c_write_blocking(I2C_PORT, CONFIG_EEPROM_ADDR, &a, 1, true);
    if (r != 1) return false;
    r = i2c_read_blocking(I2C_PORT, CONFIG_EEPROM_ADDR, buf, len, false);
    return r == (int)len;
}

/* Page-write: write at most one page, starting on a page boundary, then wait tWR. */
static bool eeprom_write_chunk(uint16_t addr, const uint8_t *buf, uint8_t len) {
    uint8_t tmp[1 + EEPROM_PAGE_SIZE];
    tmp[0] = (uint8_t)addr;
    memcpy(tmp + 1, buf, len);
    int r = i2c_write_blocking(I2C_PORT, CONFIG_EEPROM_ADDR, tmp, 1 + len, false);
    sleep_ms(EEPROM_WRITE_MS);
    return r == (int)(1 + len);
}

/* Drive a full struct transfer as page-aligned chunks so 24C02 pages never wrap. */
static bool eeprom_transfer(uint16_t base, uint8_t *buf, uint32_t len, bool write) {
    uint32_t off = 0;
    while (off < len) {
        uint16_t addr = (uint16_t)(base + off);
        uint8_t  page = EEPROM_PAGE_SIZE - (addr % EEPROM_PAGE_SIZE);
        uint8_t  chunk = (len - off < page) ? (uint8_t)(len - off) : page;

        bool ok;
        if (write) {
            ok = eeprom_write_chunk(addr, buf + off, chunk);
        } else {
            ok = eeprom_read_chunk(addr, buf + off, chunk);
        }
        if (!ok) return false;
        off += chunk;
    }
    return true;
}

void config_store_init(void) {
    bus_init();
}

bool config_store_read_map(vbfc_shadow_map_t *map) {
    bus_init();
    return eeprom_transfer(0, (uint8_t *)map, sizeof(*map), false);
}

bool config_store_write_map(const vbfc_shadow_map_t *map) {
    bus_init();
    return eeprom_transfer(0, (uint8_t *)map, sizeof(*map), true);
}

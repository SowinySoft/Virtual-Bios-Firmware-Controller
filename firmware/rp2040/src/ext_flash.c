#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"

#include "pins.h"
#include "ext_flash.h"

static spi_inst_t *ext_spi = spi1;

static void cs_select(bool active) {
    gpio_put(PIN_EXT_CS, active ? 0 : 1);
}

void ext_flash_init(void) {
    spi_init(ext_spi, 20 * 1000 * 1000);
    gpio_set_function(PIN_EXT_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_EXT_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_EXT_MISO, GPIO_FUNC_SPI);

    gpio_init(PIN_EXT_CS);
    gpio_set_dir(PIN_EXT_CS, GPIO_OUT);
    gpio_put(PIN_EXT_CS, 1);
}

void ext_flash_read_buf(uint32_t offset, uint8_t *buf, uint32_t len) {
    uint8_t header[4] = {
        0x03,
        (uint8_t)(offset >> 16),
        (uint8_t)(offset >> 8),
        (uint8_t)(offset),
    };
    cs_select(true);
    spi_write_blocking(ext_spi, header, sizeof(header));
    spi_read_blocking(ext_spi, 0, buf, len);
    cs_select(false);
}

static void write_enable(void) {
    uint8_t cmd = 0x06;
    cs_select(true);
    spi_write_blocking(ext_spi, &cmd, 1);
    cs_select(false);
}

/* Poll the status register BUSY bit instead of a fixed sleep — much faster
 * on page programs and a hard requirement for erase-range correctness. */
static void wait_while_busy(void) {
    uint8_t cmd = 0x05;          /* RDSR */
    uint8_t sr = 0x01;
    do {
        cs_select(true);
        spi_write_blocking(ext_spi, &cmd, 1);
        spi_read_blocking(ext_spi, 0, &sr, 1);
        cs_select(false);
    } while (sr & 0x01);          /* BUSY */
}

void ext_flash_sector_erase(uint32_t offset) {
    write_enable();
    uint8_t header[4] = {
        0x20,
        (uint8_t)(offset >> 16),
        (uint8_t)(offset >> 8),
        (uint8_t)(offset),
    };
    cs_select(true);
    spi_write_blocking(ext_spi, header, sizeof(header));
    cs_select(false);
    wait_while_busy();
}

void ext_flash_erase_range(uint32_t offset, uint32_t len) {
    /* Round start down and end up to sector boundaries. */
    uint32_t start = offset & ~(EXT_FLASH_SECTOR - 1);
    uint32_t end = offset + len;
    end = (end + EXT_FLASH_SECTOR - 1) & ~(EXT_FLASH_SECTOR - 1);
    for (uint32_t a = start; a < end; a += EXT_FLASH_SECTOR) {
        ext_flash_sector_erase(a);
    }
}

void ext_flash_write_buf(uint32_t offset, const uint8_t *data, uint32_t len) {
    while (len > 0) {
        uint32_t page_remain = EXT_FLASH_PAGE_SIZE - (offset & 0xFF);
        uint32_t chunk = len < page_remain ? len : page_remain;

        write_enable();
        uint8_t header[4] = {
            0x02,
            (uint8_t)(offset >> 16),
            (uint8_t)(offset >> 8),
            (uint8_t)(offset),
        };
        cs_select(true);
        spi_write_blocking(ext_spi, header, sizeof(header));
        spi_write_blocking(ext_spi, data, chunk);
        cs_select(false);
        wait_while_busy();

        offset += chunk;
        data += chunk;
        len -= chunk;
    }
}

void ext_flash_read_jedec_id(uint8_t id[3]) {
    uint8_t cmd = 0x9F;
    cs_select(true);
    spi_write_blocking(ext_spi, &cmd, 1);
    spi_read_blocking(ext_spi, 0, id, 3);
    cs_select(false);
}

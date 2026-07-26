#include <string.h>

#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"

#include "pins.h"
#include "orig_flash.h"

static spi_inst_t *orig_spi = spi0;

void orig_flash_init(void) {
    spi_init(orig_spi, 20 * 1000 * 1000);
    gpio_set_function(PIN_ORIG_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_ORIG_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_ORIG_MISO, GPIO_FUNC_SPI);

    gpio_init(PIN_ORIG_CS);
    gpio_set_dir(PIN_ORIG_CS, GPIO_OUT);
    gpio_put(PIN_ORIG_CS, 1);

    gpio_init(PIN_ORIG_SLEEP);
    gpio_set_dir(PIN_ORIG_SLEEP, GPIO_OUT);
    gpio_put(PIN_ORIG_SLEEP, 0);
}

static void cs_select(bool active) {
    gpio_put(PIN_ORIG_CS, active ? 0 : 1);
}

static void cmd_addr(uint8_t cmd, uint32_t addr) {
    uint8_t header[4] = {
        cmd,
        (uint8_t)(addr >> 16),
        (uint8_t)(addr >> 8),
        (uint8_t)(addr),
    };
    cs_select(true);
    spi_write_blocking(orig_spi, header, sizeof(header));
}

uint8_t orig_flash_read_byte(uint32_t addr) {
    uint8_t val = 0xFF;
    cmd_addr(0x03, addr);
    spi_read_blocking(orig_spi, 0, &val, 1);
    cs_select(false);
    return val;
}

void orig_flash_read_buf(uint32_t addr, uint8_t *buf, uint32_t len) {
    cmd_addr(0x03, addr);
    spi_read_blocking(orig_spi, 0, buf, len);
    cs_select(false);
}

void orig_flash_page_program(uint32_t addr, const uint8_t *data, uint32_t len) {
    uint8_t we = 0x06;
    cs_select(true);
    spi_write_blocking(orig_spi, &we, 1);
    cs_select(false);

    cmd_addr(0x02, addr);
    spi_write_blocking(orig_spi, data, len);
    cs_select(false);
    sleep_ms(3);
}

void orig_flash_sector_erase(uint32_t addr) {
    uint8_t we = 0x06;
    cs_select(true);
    spi_write_blocking(orig_spi, &we, 1);
    cs_select(false);

    cmd_addr(0x20, addr);
    cs_select(false);
    sleep_ms(100);
}

void orig_flash_read_jedec_id(uint8_t id[3]) {
    uint8_t cmd = 0x9F;
    cs_select(true);
    spi_write_blocking(orig_spi, &cmd, 1);
    spi_read_blocking(orig_spi, 0, id, 3);
    cs_select(false);
}

void orig_flash_sleep(void) {
    gpio_put(PIN_ORIG_SLEEP, 1);
}

void orig_flash_wake(void) {
    gpio_put(PIN_ORIG_SLEEP, 0);
    uint8_t cmd = 0xAB;
    cs_select(true);
    spi_write_blocking(orig_spi, &cmd, 1);
    cs_select(false);
    sleep_us(3);
}

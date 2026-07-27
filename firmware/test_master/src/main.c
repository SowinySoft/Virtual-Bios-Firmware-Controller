#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"

#define TEST_SPI_PORT spi0
#define TEST_SCK 18
#define TEST_MOSI 19
#define TEST_MISO 16
#define TEST_CS 17
#define TEST_SPI_SPEED 1000 * 1000

static void init_spi_master(void) {
    gpio_init(TEST_SCK);
    gpio_set_dir(TEST_SCK, GPIO_OUT);
    gpio_init(TEST_MOSI);
    gpio_set_dir(TEST_MOSI, GPIO_OUT);
    gpio_init(TEST_MISO);
    gpio_set_dir(TEST_MISO, GPIO_IN);
    gpio_init(TEST_CS);
    gpio_set_dir(TEST_CS, GPIO_OUT);
    gpio_put(TEST_CS, 1);

    spi_init(TEST_SPI_PORT, TEST_SPI_SPEED);
    spi_set_format(TEST_SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    spi_set_slave(TEST_SPI_PORT, false);

    gpio_set_function(TEST_SCK, GPIO_FUNC_SPI);
    gpio_set_function(TEST_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(TEST_MISO, GPIO_FUNC_SPI);
}

static void read_jedec_id(uint8_t *jedec) {
    const uint8_t cmd = 0x9F;
    uint8_t dummy = 0xFF;

    gpio_put(TEST_CS, 0);
    spi_write_blocking(TEST_SPI_PORT, &cmd, 1);
    spi_read_blocking(TEST_SPI_PORT, dummy, jedec, 3);
    gpio_put(TEST_CS, 1);
}

int main(void) {
    stdio_init_all();
    sleep_ms(500);

    init_spi_master();
    printf("VBFC test master ready\n");

    uint8_t jedec[3] = {0};
    read_jedec_id(jedec);
    printf("JEDEC: %02X %02X %02X\n", jedec[0], jedec[1], jedec[2]);

    while (true) {
        sleep_ms(1000);
        printf("heartbeat\n");
    }

    return 0;
}

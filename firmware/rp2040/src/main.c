#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include "pins.h"
#include "shadow_map.h"
#include "spi_arbiter.h"
#include "orig_flash.h"
#include "ext_flash.h"
#include "config_store.h"
#include "patch_table.h"
#include "usb_service.h"
#include "safety.h"

#define VBFC_FW_VERSION "1.0.0"

static void status_leds_init(void) {
    gpio_init(PIN_LED_OK);
    gpio_set_dir(PIN_LED_OK, GPIO_OUT);
    gpio_init(PIN_LED_FAULT);
    gpio_set_dir(PIN_LED_FAULT, GPIO_OUT);
    gpio_put(PIN_LED_OK, 1);
    gpio_put(PIN_LED_FAULT, 0);
}

static void status_leds_update(void) {
    if (safety_bypass_active() || spi_arbiter_is_passthrough()) {
        gpio_put(PIN_LED_OK, 0);
        gpio_put(PIN_LED_FAULT, 1);
        return;
    }
    gpio_put(PIN_LED_OK, 1);
    gpio_put(PIN_LED_FAULT, 0);
}

int main(void) {
    stdio_init_all();
    sleep_ms(500);

    printf("VBFC Controller v%s\n", VBFC_FW_VERSION);

    status_leds_init();
    safety_init();
    shadow_map_init();
    patch_table_init();

    if (!shadow_map_load()) {
        printf("WARN: invalid map, using factory defaults\n");
        shadow_map_factory_reset();
    }
    if (!patch_table_load()) {
        printf("WARN: invalid patch table, using defaults\n");
        patch_table_reset();
    }

#ifndef VBFC_SIM
    orig_flash_init();
    ext_flash_init();
    spi_arbiter_init();
#else
    printf("SIM: SPI arbiter disabled\n");
#endif

    usb_service_init();

    printf("Mode: %s\n",
           shadow_map_get_mode() == VBFC_MODE_SHADOW ? "shadow" :
           shadow_map_get_mode() == VBFC_MODE_HOTPATCH ? "hotpatch" :
           "pass-through");

    while (true) {
        safety_feed();
        usb_service_poll();

#ifndef VBFC_SIM
        if (!safety_bypass_active()) {
            spi_arbiter_poll();
        }
#endif

        status_leds_update();
        tight_loop_contents();
    }

    return 0;
}

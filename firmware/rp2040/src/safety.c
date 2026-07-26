#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/watchdog.h"

#include "pins.h"
#include "safety.h"
#include "spi_arbiter.h"
#include "shadow_map.h"

#define WATCHDOG_TIMEOUT_MS 500

static bool g_bypass = false;

void safety_init(void) {
    gpio_init(PIN_BYPASS);
    gpio_set_dir(PIN_BYPASS, GPIO_IN);
    gpio_pull_up(PIN_BYPASS);

    gpio_init(PIN_RESET_BTN);
    gpio_set_dir(PIN_RESET_BTN, GPIO_IN);
    gpio_pull_up(PIN_RESET_BTN);

    watchdog_enable(WATCHDOG_TIMEOUT_MS, true);
    g_bypass = gpio_get(PIN_BYPASS) == 0;
}

void safety_feed(void) {
    watchdog_update();

    if (gpio_get(PIN_BYPASS) == 0) {
        g_bypass = true;
        spi_arbiter_enable_passthrough();
    }

    if (gpio_get(PIN_RESET_BTN) == 0) {
        sleep_ms(50);
        if (gpio_get(PIN_RESET_BTN) == 0) {
            shadow_map_factory_reset();
        }
    }
}

void safety_fault(const char *reason) {
    (void)reason;
    gpio_put(PIN_LED_FAULT, 1);
    gpio_put(PIN_LED_OK, 0);
    spi_arbiter_enable_passthrough();
    g_bypass = true;
}

bool safety_bypass_active(void) {
    return g_bypass;
}

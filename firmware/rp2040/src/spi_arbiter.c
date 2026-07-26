#include <string.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include "pins.h"
#include "spi_arbiter.h"
#include "shadow_map.h"
#include "patch_table.h"
#include "sniffer.h"
#include "orig_flash.h"
#include "ext_flash.h"
#include "safety.h"
#include "image_check.h"

#define SPI_CMD_READ       0x03
#define SPI_CMD_FAST_READ  0x0B
#define SPI_CMD_PAGE_PROG  0x02
#define SPI_CMD_SECTOR_ER  0x20
#define SPI_CMD_RDID       0x9F

/* PREFETCH: cache a run of ext-flash bytes so MCU-driven reads amortize the
 * per-byte SPI transaction cost. Bust on address jump or cache exhaustion.
 * Size chosen to bound latency while fitting comfortably in SRAM. */
#define PREFETCH_BYTES 256

typedef enum {
    PHASE_IDLE,
    PHASE_CMD,
    PHASE_ADDR,
    PHASE_DUMMY,
    PHASE_DATA,
} arb_phase_t;

static volatile bool g_passthrough_mode = false;
static bool g_cs_active = false;
static bool g_sck_last = false;

static arb_phase_t g_phase = PHASE_IDLE;
static uint8_t g_cmd = 0;
static uint32_t g_addr = 0;
static uint8_t g_addr_bytes = 0;

static vbfc_map_entry_t g_active_entry;
static vbfc_source_t g_active_source = VBFC_SOURCE_ORIG;
static bool g_drive_miso = false;

static uint8_t g_shift_in = 0;
static uint8_t g_bit_in = 0;

static uint8_t g_tx_byte = 0xFF;
static uint8_t g_tx_bit = 0;

static uint32_t g_data_addr = 0;

/* Prefetch cache for MCU-driven EXT reads. `pf_valid` is false after a
 * transaction reset or address break, forcing a refill on the next byte. */
static uint8_t  pf_buf[PREFETCH_BYTES];
static uint32_t pf_base = 0;      /* ext-flash address of pf_buf[0] */
static uint32_t pf_len  = 0;      /* bytes currently cached */
static bool     pf_valid = false; /* does pf_buf hold a usable run? */

static void prefetch_invalidate(void) {
    pf_valid = false;
    pf_len = 0;
    pf_base = 0;
}

static void miso_mux_controller(bool controller_drives) {
    gpio_put(PIN_MISO_OE, controller_drives ? 0 : 1);
}

static bool is_read_cmd(uint8_t cmd) {
    return cmd == SPI_CMD_READ || cmd == SPI_CMD_FAST_READ;
}

static bool is_write_cmd(uint8_t cmd) {
    return cmd == SPI_CMD_PAGE_PROG || cmd == SPI_CMD_SECTOR_ER;
}

static void transaction_reset(void) {
    g_phase = PHASE_IDLE;
    g_cmd = 0;
    g_addr = 0;
    g_addr_bytes = 0;
    g_shift_in = 0;
    g_bit_in = 0;
    g_drive_miso = false;
    g_tx_bit = 0;
    g_active_source = VBFC_SOURCE_ORIG;
    prefetch_invalidate();
    g_sck_last = gpio_get(PIN_MB_SCK);
}

static void cs_falling(void) {
    transaction_reset();
    g_phase = PHASE_CMD;
    orig_flash_wake();
    gpio_put(PIN_ORIG_CS, 0);
    miso_mux_controller(false);
    gpio_put(PIN_CTRL_MISO, 1);
}

static void cs_rising(void) {
    /* Record the just-completed transaction before the reset zeros the fields.
     * Data byte count is derived free from address spans for transactions
     * that reached a data phase (avoiding any per-bit counter overhead). */
    if (sniffer_is_active() && g_cmd != 0) {
        uint8_t flags = 0;
        if (is_read_cmd(g_cmd))  flags |= 0x01;
        if (is_write_cmd(g_cmd)) flags |= 0x02;
        if (g_addr_bytes >= 3)   flags |= 0x04;
        uint16_t cnt = 0;
        if (g_phase == PHASE_DATA) {
            uint32_t span = (g_data_addr >= g_addr) ? (g_data_addr - g_addr) : 0;
            cnt = (span > 0xFFFF) ? 0xFFFF : (uint16_t)span;
        }
        sniffer_record(g_cmd, flags, g_addr, cnt);
    }
    gpio_put(PIN_ORIG_CS, 1);
    orig_flash_wake();
    miso_mux_controller(false);
    g_drive_miso = false;
    transaction_reset();
}

static void load_tx_byte(void) {
    if (g_active_source == VBFC_SOURCE_EXT) {
        /* EXT path with prefetch: serve from cache, refill on miss/break. */
        uint32_t rel = g_data_addr - g_active_entry.start_addr;
        uint32_t ext_addr = g_active_entry.ext_offset + rel;

        bool hit = pf_valid &&
                   ext_addr >= pf_base &&
                   ext_addr < pf_base + pf_len;
        if (!hit) {
            uint32_t remain = EXT_FLASH_SIZE - ext_addr;
            uint32_t want = (remain < PREFETCH_BYTES) ? remain : PREFETCH_BYTES;
            ext_flash_read_buf(ext_addr, pf_buf, want);
            pf_base = ext_addr;
            pf_len = want;
            pf_valid = true;
        }
        g_tx_byte = pf_buf[ext_addr - pf_base];

        /* Even EXT bytes may be remapped by a patch targeting the same
         * flash address — apply it so patches "just work" regardless of source. */
        uint8_t patched;
        if (patch_table_match(ext_addr, g_tx_byte, &patched)) {
            g_tx_byte = patched;
        }
    } else {
        /* ORIG path: read the real byte, then let the patch table override it.
         * Only meaningful in HOTPATCH mode (the control flow that reaches here
         * for ORIG only engages under HOTPATCH — see enter_data_phase). */
        g_tx_byte = orig_flash_read_byte(g_data_addr);
        uint8_t patched;
        if (patch_table_match(g_data_addr, g_tx_byte, &patched)) {
            g_tx_byte = patched;
        }
    }
    g_tx_bit = 0;
}

static void enter_data_phase(void) {
    g_data_addr = g_addr;
    g_phase = PHASE_DATA;

    if (g_cmd == SPI_CMD_RDID) {
        g_drive_miso = false;
        miso_mux_controller(false);
        return;
    }

    vbfc_mode_t mode = shadow_map_get_mode();
    if (mode != VBFC_MODE_SHADOW && mode != VBFC_MODE_HOTPATCH) {
        g_drive_miso = false;
        miso_mux_controller(false);
        return;
    }

    g_active_source = shadow_map_lookup(g_addr, &g_active_entry);
    prefetch_invalidate();

    /* Phase A: signature gate. If the looked-up EXT bank hasn't been
     * verified this boot (via image_check_verify_on_boot or a prior
     * verified ULOAD), don't serve it — fall to the ORIG pass-through
     * path below. The motherboard reads the real flash for this
     * transaction. This is a per-transaction drop, not a permanent
     * safety_fault(). */
    bool sig_ok = true;
    if (g_active_source == VBFC_SOURCE_EXT && !image_check_serve_allowed(g_active_entry.ext_offset)) {
        sig_ok = false;
    }

    if (is_read_cmd(g_cmd) && g_active_source == VBFC_SOURCE_EXT && sig_ok) {
        orig_flash_sleep();
        gpio_put(PIN_ORIG_CS, 1);
        miso_mux_controller(true);
        g_drive_miso = true;
        load_tx_byte();
        return;
    }

    if (is_read_cmd(g_cmd) && g_active_source == VBFC_SOURCE_ORIG &&
        mode == VBFC_MODE_HOTPATCH) {
        /* MCU takes over MISO to read the real byte from ORIG and substitute
         * any matching patch before forwarding to the motherboard. */
        orig_flash_wake();
        miso_mux_controller(true);
        g_drive_miso = true;
        load_tx_byte();
        return;
    }

    if (is_write_cmd(g_cmd) && g_active_source == VBFC_SOURCE_EXT) {
        orig_flash_sleep();
        gpio_put(PIN_ORIG_CS, 1);
        g_drive_miso = false;
        miso_mux_controller(false);
        return;
    }

    g_drive_miso = false;
    orig_flash_wake();
    miso_mux_controller(false);
}

static void handle_write_data_byte(uint8_t data) {
    if (g_active_source == VBFC_SOURCE_EXT) {
        uint32_t off = g_active_entry.ext_offset +
                       (g_data_addr - g_active_entry.start_addr);
        ext_flash_write_buf(off, &data, 1);
    } else if (g_cmd == SPI_CMD_PAGE_PROG) {
        orig_flash_page_program(g_data_addr, &data, 1);
    }
    g_data_addr++;
}

static void handle_complete_byte(uint8_t byte) {
    switch (g_phase) {
    case PHASE_CMD:
        g_cmd = byte;
        if (g_cmd == SPI_CMD_RDID) {
            g_phase = PHASE_DATA;
            g_drive_miso = false;
            miso_mux_controller(false);
        } else {
            g_phase = PHASE_ADDR;
            g_addr = 0;
            g_addr_bytes = 0;
        }
        break;

    case PHASE_ADDR:
        g_addr = (g_addr << 8) | byte;
        g_addr_bytes++;
        if (g_addr_bytes >= 3) {
            if (g_cmd == SPI_CMD_FAST_READ) {
                g_phase = PHASE_DUMMY;
            } else if (is_write_cmd(g_cmd)) {
                g_active_source = shadow_map_lookup(g_addr, &g_active_entry);
                if (g_active_source == VBFC_SOURCE_EXT) {
                    orig_flash_sleep();
                    gpio_put(PIN_ORIG_CS, 1);
                }
                g_data_addr = g_addr;
                g_phase = PHASE_DATA;
            } else {
                enter_data_phase();
            }
        }
        break;

    case PHASE_DUMMY:
        enter_data_phase();
        break;

    case PHASE_DATA:
        if (is_write_cmd(g_cmd)) {
            handle_write_data_byte(byte);
        }
        break;

    default:
        break;
    }
}

static void snoop_bit(bool mosi) {
    g_shift_in = (uint8_t)((g_shift_in << 1) | (mosi ? 1 : 0));
    g_bit_in++;
    if (g_bit_in >= 8) {
        handle_complete_byte(g_shift_in);
        g_shift_in = 0;
        g_bit_in = 0;
    }
}

static void miso_drive_bit(void) {
    if (!g_drive_miso) {
        return;
    }
    uint8_t bit = (uint8_t)((g_tx_byte >> (7 - g_tx_bit)) & 1);
    gpio_put(PIN_CTRL_MISO, bit);
    g_tx_bit++;
    if (g_tx_bit >= 8) {
        g_data_addr++;
        load_tx_byte();
    }
}

void spi_arbiter_enable_passthrough(void) {
    g_passthrough_mode = true;
    miso_mux_controller(false);
    orig_flash_wake();
    gpio_put(PIN_ORIG_CS, 1);
}

bool spi_arbiter_is_passthrough(void) {
    return g_passthrough_mode || (gpio_get(PIN_BYPASS) == 0);
}

void spi_arbiter_init(void) {
    gpio_init(PIN_MB_CS);
    gpio_set_dir(PIN_MB_CS, GPIO_IN);
    gpio_pull_up(PIN_MB_CS);

    gpio_init(PIN_MB_SCK);
    gpio_set_dir(PIN_MB_SCK, GPIO_IN);

    gpio_init(PIN_MB_MOSI);
    gpio_set_dir(PIN_MB_MOSI, GPIO_IN);

    gpio_init(PIN_CTRL_MISO);
    gpio_set_dir(PIN_CTRL_MISO, GPIO_OUT);
    gpio_put(PIN_CTRL_MISO, 1);

    gpio_init(PIN_MISO_OE);
    gpio_set_dir(PIN_MISO_OE, GPIO_OUT);
    miso_mux_controller(false);

    gpio_init(PIN_BYPASS);
    gpio_set_dir(PIN_BYPASS, GPIO_IN);
    gpio_pull_up(PIN_BYPASS);

    g_passthrough_mode = false;
    g_cs_active = false;
    g_sck_last = false;
    transaction_reset();
}

void spi_arbiter_poll(void) {
    if (spi_arbiter_is_passthrough()) {
        return;
    }

    bool cs = gpio_get(PIN_MB_CS) == 0;

    if (cs && !g_cs_active) {
        cs_falling();
    } else if (!cs && g_cs_active) {
        cs_rising();
    }
    g_cs_active = cs;

    if (!cs) {
        g_sck_last = gpio_get(PIN_MB_SCK);
        return;
    }

    bool sck = gpio_get(PIN_MB_SCK);
    bool mosi = gpio_get(PIN_MB_MOSI);

    if (sck && !g_sck_last) {
        if (g_phase != PHASE_IDLE && g_phase != PHASE_DATA) {
            snoop_bit(mosi);
        } else if (g_phase == PHASE_DATA && is_write_cmd(g_cmd)) {
            snoop_bit(mosi);
        }
    }

    if (!sck && g_sck_last && g_phase == PHASE_DATA && g_drive_miso) {
        miso_drive_bit();
    }

    g_sck_last = sck;
}

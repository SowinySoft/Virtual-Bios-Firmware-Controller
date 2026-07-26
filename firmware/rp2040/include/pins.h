#ifndef VBFC_PINS_H
#define VBFC_PINS_H

/* SPI master to original flash — shares bus with MB on breadboard (GP0/1/0) */
#define PIN_ORIG_SCK   0
#define PIN_ORIG_MOSI  1
#define PIN_ORIG_MISO  2   /* Direct read from orig chip SO (idle access) */
#define PIN_ORIG_CS    3   /* CS out to original chip */

/* Motherboard / test-master interface */
#define PIN_MB_CS      4   /* CS sense (input, active low) */
#define PIN_MISO_OE    5   /* MISO mux OE (active low = controller drives) */
#define PIN_ORIG_SLEEP 6   /* High = sleep / deselect original chip */

/* Bus snoop lines (parallel to pass-through SCK/MOSI) */
#define PIN_MB_SCK     7
#define PIN_MB_MOSI    8

/* Controller MISO output to mux (separate from orig MISO read pin) */
#define PIN_CTRL_MISO  9

/* Extension flash (SPI1) */
#define PIN_EXT_SCK    10
#define PIN_EXT_MOSI   11
#define PIN_EXT_MISO   12
#define PIN_EXT_CS     13

/* I2C config EEPROM (optional on breadboard) */
#define PIN_I2C_SDA    14
#define PIN_I2C_SCL    15
#define I2C_PORT       i2c0
#define EEPROM_ADDR    0x50

/* Status */
#define PIN_LED_OK     16
#define PIN_LED_FAULT  17
#define PIN_BYPASS     18
#define PIN_RESET_BTN  19

#endif /* VBFC_PINS_H */

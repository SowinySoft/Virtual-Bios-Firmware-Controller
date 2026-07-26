# VBFC Interposer Netlist Reference

## SPI Bus (Motherboard ↔ Interposer)

| Net | MB Pin | Interposer | Notes |
|-----|--------|------------|-------|
| SPI_CS# | 1 | MB_CS_IN, via R3 pull-up | Sense + override |
| SPI_MISO | 2 | MISO_BUS | Shared bus |
| SPI_WP# | 3 | WP_PASS | Direct pass-through |
| GND | 4 | GND | |
| SPI_MOSI | 5 | MOSI_PASS | Direct pass-through |
| SPI_SCK | 6 | SCK_PASS | Direct pass-through |
| SPI_HOLD# | 7 | HOLD_PASS | Direct pass-through |
| VCC | 8 | VCC_3V3 | From MB or LDO |

## Original Chip Interface (RP2040 SPI0 Master)

| Net | RP2040 | Original Chip |
|-----|--------|---------------|
| ORIG_SCK | GP0 | Pin 6 |
| ORIG_MOSI | GP1 | Pin 5 |
| ORIG_MISO | GP2 | Pin 2 |
| ORIG_CS# | GP3 | Pin 1 |
| ORIG_SLEEP | GP6 | Pin 1 (override via OR) |

## MISO Mux

```
Original MISO ──► 74LVC1G125 A
RP2040 MISO   ──► 74LVC1G125 Y ──► MISO_BUS (to MB)
OE#           ◄── GP5
```

When controller responds: OE# low, RP2040 drives MISO.
When pass-through: OE# high, original chip drives MISO.

## CS# Override (SPI Spy technique)

Many boards have series resistor on CS#. RP2040 GP6 can drive CS# high to
sleep the original chip when the controller takes over the bus.

## Bypass Jumper J1

```
Closed:  MB_* ──direct──► ORIG_*  (all signals)
Open:    signals route through arbiter logic
```

## Extension Flash (RP2040 SPI1)

| Net | RP2040 | W25Q128 |
|-----|--------|---------|
| EXT_SCK | GP10 | Pin 6 |
| EXT_MOSI | GP11 | Pin 5 |
| EXT_MISO | GP12 | Pin 2 |
| EXT_CS# | GP13 | Pin 1 |

## I²C Config EEPROM

| Net | RP2040 | 24C02 |
|-----|--------|-------|
| SDA | GP14 | Pin 5 |
| SCL | GP15 | Pin 6 |

I²C address: 0x50

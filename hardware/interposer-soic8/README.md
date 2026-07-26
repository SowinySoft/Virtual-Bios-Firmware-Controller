# SOIC-8 SPI Interposer PCB

Physical adapter that sits between the motherboard BIOS socket pads and the
original SPI NOR flash chip.

## Board Stack

```
  [ Original BIOS chip (SOIC-8) ]
  [ VBFC interposer PCB (~1.0 mm FR4) ]
  [ Motherboard SOIC-8 pads ]
```

## Block Diagram

```
                    Motherboard
                    CS# CLK MOSI MISO WP# HOLD#
                         |   |    |    |   |    |
                    +----+---+----+----+---+----+
                    |         VBFC Interposer    |
                    |                            |
    RP2040 GPIO ----+-- CS override              |
    RP2040 SPI0  ----+-- Orig chip SPI master ----+---> Original BIOS
    RP2040 SPI1  ----+-- Ext flash (W25Q128)     |
    74LVC1G125   ----+-- MISO tri-state mux       |
    Jumper J1    ----+-- Bypass (direct passthrough)
                    +----------------------------+
                         USB-C (RP2040 native)
```

## GPIO Assignment (RP2040)

| GPIO | Function |
|------|----------|
| GP0 | SPI0 SCK → original chip |
| GP1 | SPI0 TX (MOSI) → original chip |
| GP2 | SPI0 RX (MISO) ← original chip |
| GP3 | Original CS# (master out) |
| GP4 | MB CS# sense (slave in) |
| GP5 | MISO mux OE (active low) |
| GP6 | Original CS# override (sleep chip) |
| GP10 | SPI1 SCK → W25Q128 |
| GP11 | SPI1 TX → W25Q128 |
| GP12 | SPI1 RX ← W25Q128 |
| GP13 | Ext flash CS# |
| GP14 | I²C SDA → 24C02 |
| GP15 | I²C SCL → 24C02 |
| GP16 | Status LED (green) |
| GP17 | Status LED (red) |
| GP18 | Bypass sense (jumper) |
| GP19 | Factory reset button |

## Bypass Jumper (J1)

When **closed**, MISO/MOSI/SCK/CS# route directly from MB to original chip,
completely bypassing the RP2040 arbiter. Use for recovery.

## Design Rules

- Trace length MB ↔ chip: < 25 mm
- Controlled impedance not required at ≤ 50 MHz single-SPI
- Decoupling: 100 nF on each IC VCC pin + 10 µF bulk
- USB-C: CC1/CC2 with 5.1 kΩ pull-down (device mode)

## Files

| File | Description |
|------|-------------|
| `bom.csv` | Bill of materials |
| `schematic.md` | Netlist reference (KiCad schematic pending) |

## KiCad

Full KiCad project files will be added in a follow-up once the MVP firmware
validates the GPIO map on a breadboard prototype.

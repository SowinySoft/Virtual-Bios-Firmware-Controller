# VBFC RP2350 Rev 2 — Recommended Immediate Next Step

## Purpose
Migrate the VBFC design from RP2040 to RP2350 (dual-core Arm Cortex-M33 @150 MHz + 2×PIO) to enable:
- 8-line Octal-SPI (OPI) support on a single MCU (eliminates need for external mux)
- USB 1.1 Full-Speed + Device + Host on the same die
- 48+ GPIO (vs 26 on RP2040) for mux control, EEPROM, debug, and test points
- Hardened crypto (hardware-accelerated CRC32, SHA-256, ECC M33)

## Hardware Spec
- MCU: RP2350-A (replacing RP2040)
- 8-line SPI mux: TS3A5017 or equivalent (or connect OPI directly to flash)
- EEPROM: 24C02 (unchanged) — now accessed via dedicated GPIO lines
- USB: RP2350 D+/D- pins (same physical connectors)
- Power: Same 3.3 V supply; add POR reset supervisor (e.g., TPS3825)

## GPIO Map (RP2350 vs RP2040)
| Signal | RP2040 GPIO | RP2350 GPIO | Notes |
|--------|-------------|-------------|-------|
| SPI CS0  | 0           | 1           |       |
| SPI SCK  | 1           | 2           |       |
| SPI MOSI | 2           | 3           |       |
| SPI MISO | 4           | 4           |       |
| MUX SEL  | 5           | 5           |       |
| EEPROM WP| 6           | 6           |       |
| USB D+   | 24          | 7           |       |
| USB D-   | 25          | 8           |       |
| DEBUG LED| 26          | 9           |       |

## Migration Roadmap
1. Update RP2040 firmware (PIO → RP2350 PIO v2) — same state machine, more pins available
2. Update host-tool `vbfc-host` to map new GPIO offsets
3. Update `docs/vbfc_paper_full/sections_full/07_implementation.tex` with revised pinout table
4. Add `docs/vbfc_rp2350_rev2/hardware_spec.md` with full schematic notes
5. Benchmark OPI read latency vs RP2040 QSPI (expected: 2–3× speedup)

## Links
- Paper Section 9.1 (FPGA) and 9.2 (Phase B) superseded by this concrete hardware revision
- RP2350 Datasheet: https://www.raspberrypi.com/documentation/microcontrollers.html#rp2350

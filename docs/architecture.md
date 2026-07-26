# VBFC Architecture

Virtual BIOS Firmware Controller (VBFC) is a programmable SPI interposer that sits
between a motherboard and its existing BIOS NOR flash chip. The controller preserves
normal boot behavior by default and adds one programmable 64 KB shadow region.

## System Context

```
Motherboard (SPI Master)
        |
   [ VBFC Interposer ]
    /              \
Original BIOS    Extension Flash
   chip           (W25Q128)
        \
      USB-C (programming)
```

The platform always sees a single SPI flash device. The controller decides, per
address, whether to serve data from the original chip or the extension store.

## Hardware Tier (MVP)

| Component | Part | Role |
|-----------|------|------|
| MCU | RP2040 | SPI arbiter, USB CDC, configuration |
| Extension flash | W25Q128 (16 MB) | Shadow image storage |
| Level shifters | 74LVC1T45 (if needed) | 1.8 V / 3.3 V compatibility |
| MISO mux | 74LVC1G125 + GPIO | Tri-state bus sharing |
| Config | 24C02 EEPROM | Shadow map persistence |
| Bypass | 2-pin header jumper | Hardware pass-through fallback |

## Firmware Modules

```
main.c
  +-- spi_arbiter.c    Decode SPI commands, route transactions
  +-- shadow_map.c     Address range table (ORIG | EXT)
  +-- ext_flash.c      W25Q128 driver (extension store)
  +-- orig_flash.c     Pass-through SPI master to original chip
  +-- config_store.c   EEPROM persistence
  +-- usb_service.c    Host protocol over USB CDC
  +-- safety.c         Watchdog + fault → pass-through
```

## Operating Modes

| Mode | Value | Behavior |
|------|-------|----------|
| Pass-through | `0x00` | All transactions forwarded to original chip |
| Shadow | `0x01` | Shadow region served from extension flash |
| Dev emulate | `0x02` | Reserved for FPGA tier |

Default on power-up: **Pass-through** until shadow map is loaded and validated.

## SPI Arbitration Flow

```
CS# asserted by motherboard
  → Capture command byte + address
  → Lookup address in shadow map
  → If ORIG: forward transaction to original chip, relay MISO
  → If EXT:  read/write extension flash directly
  → If fault: assert bypass (pass-through only)
```

## JEDEC ID Policy

`RDID (0x9F)` and `REMS (0x90)` always return the **original chip** JEDEC ID.
This keeps vendor BIOS tools and chipset probes unchanged.

## Safety

1. **Hardware bypass jumper** — shorts controller out of SPI data path.
2. **Watchdog** — arbiter fault forces pass-through within 1 ms.
3. **Factory reset** — clears shadow map, restores pass-through mode.
4. **Write protect** — shadow region writes never touch original chip.

## Future (FPGA Tier)

Quad-SPI, full-image dev emulate, and sub-microsecond timing will move to an
ECP5-based board sharing the same shadow map format and USB protocol.

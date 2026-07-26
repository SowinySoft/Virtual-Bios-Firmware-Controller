# VBFC RP2040 Firmware

Programmable SPI interposer firmware for the Virtual BIOS Firmware Controller.

## Requirements

- [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) (v2.0+)
- ARM GCC toolchain
- CMake 3.13+

## Build

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
cd firmware/rp2040
mkdir build && cd build
cmake ..
make -j4
```

Output: `vbfc_controller.uf2` — flash via BOOTSEL USB.

## Module Overview

| File | Purpose |
|------|---------|
| `main.c` | Init, main loop, mode dispatch |
| `spi_arbiter.c` | SPI slave decode + routing |
| `shadow_map.c` | Address map load/validate/lookup |
| `orig_flash.c` | SPI master pass-through to original chip |
| `ext_flash.c` | W25Q128 extension store driver |
| `config_store.c` | EEPROM persistence |
| `usb_service.c` | Host protocol (CDC) |
| `safety.c` | Watchdog + fault handler |
| `pins.h` | GPIO pin definitions |

## Development Without Hardware

Use `VBFC_SIM=1` build flag to compile with simulated SPI for unit testing
on Pico board connected via USB only.

```bash
cmake -DVBFC_SIM=1 ..
make
```

## USB Protocol

Text-based line protocol over CDC (115200 baud default):

```
> PING
< PONG vbfc-1.0.0

> GET MODE
< MODE pass-through

> SET MODE shadow
< OK

> GET MAP
< MAP 1 0xFF0000 65536 ext 0x0

> UPLOAD 0 4096
< READY
< (binary 4096 bytes)
< OK

> FACTORY RESET
< OK
```

See `docs/shadow-map-spec.md` for binary map format.

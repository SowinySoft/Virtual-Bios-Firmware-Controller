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
cmake -G Ninja ..
ninja
```

If picotool is available (host compiler present), the build produces:
`vbfc_controller.uf2` — flash via BOOTSEL USB.

If picotool is absent (e.g. no host C/C++ compiler), pass `-DPICO_NO_PICOTOOL=1`
to CMake — the firmware `.elf` and `.bin` will still link, and the `.uf2` can
be generated separately from the `.bin` with the included Python converter:

```bash
python host/vbfc-cli/vbfc_cli/vbfc_uf2.py firmware/rp2040/build/vbfc_controller.bin firmware/rp2040/build/vbfc_controller.uf2
```

## Module Overview

| File | Purpose |
|------|---------|
| `main.c` | Init, main loop, mode dispatch, status LEDs |
| `spi_arbiter.c` | Bit-bang SPI slave: decodes RDID/READ/FAST_READ/PAGE_PROG/SECTOR_ER, routes ORIG/EXT by address, 256-byte EXT prefetch cache, MISO mux, in-flight patch substitution |
| `shadow_map.c` | 4-entry address map with CRC32 validation, load/save, factory defaults, ORIG/EXT lookup |
| `orig_flash.c` | SPI master driver for the original BIOS chip (read/program/erase, sleep/wake) |
| `ext_flash.c` | W25Q128 extension image-store driver (page-program, sector erase, RDSR busy-poll) |
| `patch_table.c` | 64-entry hot-patch table (CRC32, dedup, swap-remove); `patch_table_match` applied in the arbiter read path |
| `sniffer.c` | Transaction sniffer — 4 KB ring (512 events × 8 B), base64+CRC32 chunked dump |
| `proto_binary.c` | Binary framing layer — `ULOAD START/CHUNK/DONE` upload + `DUMP EXT/ORIG/SNIFF` download (base64, running CRC) |
| `usb_service.c` | USB CDC text command service (mode/map/patch/flash/sniff/status) delegating binary work to `proto_binary.c` |
| `config_store.c` | 24C02 I²C EEPROM persistence of the shadow map (page-aligned transfers) |
| `b64.c` | Base64 encoder (used by dump paths) |
| `vbfc_crc.c` | CRC32 helper (used by dumps) |
| `safety.c` | Watchdog feed, bypass jumper, factory-reset button, fault handler |
| `pins.h` | GPIO pin map |

## Development Without Hardware

Use `VBFC_SIM=1` build flag to compile with simulated SPI for unit testing
on Pico board connected via USB only.

```bash
cmake -DVBFC_SIM=1 ..
make
```

## USB Protocol

Line-oriented text protocol over USB CDC. Replies end lines with `\r\n` so the
host's `readline()` can stream them uniformly. Most commands reply with a
terminal `OK` or `ERR <why>` line. Binary payloads (image upload/download)
are framed as ASCII base64 `CHUNK` lines so the whole channel stays text —
there is no raw-binary mode.

```
> PING
< PONG vbfc-1.1.0

> VERSION
< VERSION firmware 1.1.0
< VERSION proto text+b64-v1
< OK

> STATUS
< STATUS mode shadow
< STATUS vox-bypass off
< STATUS orig-jedec 0xEF 0x40 0x18
< STATUS ext-jedec  0xEF 0x40 0x18
< STATUS map-entries 1
< STATUS patch-entries 0
< STATUS sniff off
< OK

> GET MODE
< MODE shadow
> SET MODE hotpatch        (other modes: shadow | pass-through)
< OK

> MAP ADD 0xFF0000 65536 ext 0
< OK
> GET MAP
< MAP 0 0x00FF0000 65536 ext 0x0
< OK
> MAP REMOVE 0 | MAP CLEAR

> PATCH ADD 0xFF0010 0xFF 0xEB       (orig=0xFF = wildcard, no verify)
< OK
> PATCH LIST
< PATCH 0 0x00FF0010 0xFF 0xEB on
< OK
> PATCH REMOVE 0 | PATCH CLEAR

> SNIFF start | stop | status | dump
< SNIFF on events 12 / 512
< OK

> FLASH ERASE 0x2000 4096            (ext flash; sectors rounded)
< OK
> FLASH READ ext 0x2000 256          (streams DUMP EXT framing, see below)
> FLASH BACKUP [len]                 (default 8 MB; dumps original chip)
> FLASH RESTORE 0x0 8388608          (prints a plan; host then drives ULOAD)

> REBOOT | FACTORY RESET
```

### Binary framing — uploads (host → device)

```
> ULOAD START <offset> <total_len>          (offset must be >= 0x2000)
< OK upload-started <total_len>             (ext-flash covering range erased)
> ULOAD CHUNK <base64 up to 768 raw bytes>
< OK <received_so_far>
> ULOAD CHUNK ...
> ULOAD DONE <expected_crc32>
< OK upload-done                             (or ERR length a/b / ERR crc X != Y)
```

### Binary framing — dumps (device → host)

```
> DUMP EXT <offset> <len>     | DUMP ORIG <offset> <len>     | DUMP SNIFF
< DUMP EXT START 0x002000 4096
< DUMP CHUNK <base64 ...>
< DUMP CHUNK ...
< DUMP DONE 0xDEADBEEF           (crc32 of the decoded bytes)
```

A sniffer dump streams the frozen 4 KB ring as base64 chunks under the same
`DUMP SNIFF START <n>` / `DUMP CHUNK` / `DUMP DONE <crc32>` shape; each 8-byte
event record packs `(u8 cmd, u8 flags, u24 addr, u16 count)` little-endian.

See `docs/shadow-map-spec.md` for the binary map format stored in EEPROM and
the ext-flash metadata sector.

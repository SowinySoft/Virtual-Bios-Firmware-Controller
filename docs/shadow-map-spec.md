# Shadow Map Specification v1

The shadow map defines which SPI flash address ranges are served from the
extension store instead of the original BIOS chip.

## Storage

- Primary: I²C EEPROM (24C02, 256 bytes)
- Backup: first 4 KB sector of extension flash (mirrored on save)

## Map Entry Format

Each entry is 12 bytes. Maximum 4 entries in v1 (48 bytes + 16 byte header).

```
Offset  Size  Field
------  ----  -----
0       4     magic          "VBFC" (0x56424643)
4       1     version        0x01
5       1     entry_count    0–4
6       1     mode           0=pass-through, 1=shadow
7       1     flags          bit0=write_enable shadow
8       4     crc32          CRC of entries
12      12*N  entries
```

### Entry (12 bytes)

```
Offset  Size  Field
------  ----  -----
0       4     start_addr     Byte address (24-bit used, top byte 0)
4       4     size           Region size in bytes (must be power of 2)
8       1     source         0=ORIG, 1=EXT, 2=OVERLAY (reserved)
9       3     ext_offset     Offset in extension flash (24-bit)
```

## Default Map (Factory)

```
mode:        pass-through (0x00)
entry_count: 1
entry[0]:
  start_addr:  0x00FF0000
  size:        65536 (64 KB)
  source:      EXT (disabled until mode=shadow)
  ext_offset:  0x000000
```

The default 64 KB region at `0xFF0000` is a common top-of-flash location for
Option ROM space on 16 MB images. Adjust per target board during validation.

## Validation Rules

1. Entries must not overlap.
2. `size` must be power of 2, minimum 4096.
3. `start_addr` must be aligned to `size`.
4. `ext_offset + size` must fit in extension flash (16 MB).
5. CRC must match or map is rejected → pass-through fallback.

## CRC32

Polynomial: IEEE 802.3 (0xEDB88320), init 0xFFFFFFFF, final XOR 0xFFFFFFFF.
CRC covers bytes 12..(12 + entry_count * 12 - 1).

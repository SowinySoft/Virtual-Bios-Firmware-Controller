#!/usr/bin/env python3
"""
vbfc-uf2 — convert a raw RP2040 flash image (.bin) to an UF2 firmware file.

UF2 is the 512-byte-block firmware format Microsoft designed for flashing
microcontrollers over USB Mass Storage (the Pico's BOOTSEL mode mounts a
drive and you drop the .uf2 onto it). The Pico SDK normally generates this
via picotool, which requires a host C++ compiler to build. This script
replaces that one step so the firmware can be flashed without picotool.

Format reference: https://github.com/microsoft/uf2

Usage:
    python vbfc-uf2.py <input.bin> <output.uf2> [base_addr]
                       default base_addr = 0x10000000 (RP2040 external flash)

The input .bin must be the concatenated flash image produced by the SDK
(objcopy of .text + .data), dropped at `base_addr`. The standard Pico SDK
link places the image at 0x10000000.
"""
import struct
import sys

# UF2 block header (part of the documented spec).
UF2_MAGIC_START_0 = 0x0A324655
UF2_MAGIC_START_1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
# Block flags: bit 0 = "this block contains a main-payload, not a file" (we
# always set it for a firmware image). bit 12 = "extension tags present" (no).
UF2_FLAG_MAIN = 0x00002000
# RP2040 family ID (so a board that checks families accepts the image).
# Documented value; matches the SDK/CircuitPython tooling.
RP2040_FAMILY_ID = 0xE48BFF56

PAYLOAD = 256          # bytes of payload per 512-byte UF2 block
BLOCK = 512
HEADER = 32            # bytes
TRAILER = 4            # trailing magic
# 512 - 32 - 256 - 4 = 220 bytes of zero-padding between payload and trailer.
# Total: header(32) + payload(256) + padding(220) + trailing_magic(4) = 512.
PAD_BYTES = BLOCK - HEADER - PAYLOAD - TRAILER  # = 220


def uf2(bin_path: str, uf2_path: str, base_addr: int = 0x10000000) -> int:
    with open(bin_path, "rb") as f:
        data = f.read()
    if not data:
        raise SystemExit("error: input bin is empty")

    n = len(data)
    numblocks = (n + PAYLOAD - 1) // PAYLOAD
    datapadding = b"\x00" * PAD_BYTES
    out = bytearray()
    for blockno in range(numblocks):
        ptr = blockno * PAYLOAD
        chunk = data[ptr:ptr + PAYLOAD]
        flags = UF2_FLAG_MAIN | 0x2000   # 0x2000 = "familyID present"
        target = base_addr + ptr
        # 32-byte UF2 header. The family ID lives in field 7 (index 7, offset
        # 28) of the standard uf2conf.py header — it is part of the 32 bytes,
        # not an appendix. (matches the upstream microsoft/uf2 utils/uf2conv.py)
        hdr = struct.pack(
            "<IIIIIIII",
            UF2_MAGIC_START_0,   # [0] 0x0A324655
            UF2_MAGIC_START_1,   # [1] 0x9E5D5157
            flags,               # [2] 0x2000 | UF2_FLAG_MAIN
            target,              # [3] flash absolute address
            PAYLOAD,             # [4] payload capacity (always 256)
            blockno,             # [5] current block number
            numblocks,           # [6] total blocks
            RP2040_FAMILY_ID,    # [7] RP2040 family (0xE48BFF56)
        )
        assert len(hdr) == HEADER
        # Pad the last chunk to a full 256 bytes.
        while len(chunk) < PAYLOAD:
            chunk += b"\x00"
        block = hdr + chunk + datapadding + struct.pack("<I", UF2_MAGIC_END)
        assert len(block) == BLOCK, f"block drift: {len(block)}"
        out += block

    with open(uf2_path, "wb") as f:
        f.write(out)
    return numblocks


def main(argv: list[str]) -> int:
    if len(argv) < 3:
        print(__doc__)
        return 2
    bin_path, uf2_path = argv[1], argv[2]
    base_addr = int(argv[3], 0) if len(argv) >= 4 else 0x10000000
    blocks = uf2(bin_path, uf2_path, base_addr)
    print(f"wrote {uf2_path}: {blocks} blocks, "
          f"{blocks * BLOCK} bytes (base 0x{base_addr:08X})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))

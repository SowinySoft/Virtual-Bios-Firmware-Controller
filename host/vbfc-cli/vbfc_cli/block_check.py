#!/usr/bin/env python3
"""Defective-block detector — CRC32 blocks of a ROM, diff against known-good.

Usage:
    vbfc-cli check <romfile> [--good <reference.rom>] [--block-size 4k|64k]

Pure host-side: splits a ROM dump into power-of-two blocks, CRC32 each,
and optionally diffs against a known-good reference (e.g. the downloaded
OEM image) to flag blocks that differ.
"""
import hashlib
import zlib
from pathlib import Path

BLOCK_SIZES = {"4k": 4096, "64k": 65536, "256k": 262144, "1m": 1048576}


def check_rom(rom: bytes, good: bytes | None = None,
              block_size: int = 4096, expected_size: int | None = None) -> dict:
    result = {
        "block_size": block_size,
        "total_blocks": 0,
        "bad_blocks": [],
        "good_blocks": 0,
        "total_crc": None,
        "sha256": hashlib.sha256(rom).hexdigest(),
        "size": len(rom),
        "size_ok": None,
        "compliance": False,
    }

    # Size compliance check
    if expected_size is not None:
        result["size_ok"] = (len(rom) == expected_size)
        if not result["size_ok"]:
            result["bad_blocks"].append({
                "index": -1,
                "offset": 0,
                "size": len(rom),
                "rom_crc32": "",
                "good_crc32": "",
                "severity": "size_mismatch",
            })
    else:
        # No expected size provided: flag if size is non-standard
        std_sizes = [256*1024, 512*1024, 1024*1024, 2*1024*1024, 4*1024*1024,
                     8*1024*1024, 16*1024*1024, 32*1024*1024]
        result["size_ok"] = len(rom) in std_sizes or (len(rom) % 4096 == 0)
        if not result["size_ok"]:
            result["bad_blocks"].append({
                "index": -1,
                "offset": 0,
                "size": len(rom),
                "rom_crc32": "",
                "good_crc32": "",
                "severity": "size_mismatch",
            })
    n = len(rom)
    blocks = (n + block_size - 1) // block_size
    result["total_blocks"] = blocks

    # CRC32 each block
    block_crcs = []
    for i in range(blocks):
        chunk = rom[i * block_size:(i + 1) * block_size]
        crc = zlib.crc32(chunk) & 0xFFFFFFFF
        block_crcs.append(crc)

    result["total_crc"] = zlib.crc32(rom) & 0xFFFFFFFF

    # If a known-good reference is provided, diff
    if good is not None:
        good_blocks = (len(good) + block_size - 1) // block_size
        for i in range(min(blocks, good_blocks)):
            off = i * block_size
            chunk = rom[off:off + block_size]
            good_chunk = good[off:off + block_size]
            gcrc = zlib.crc32(good_chunk) & 0xFFFFFFFF
            if block_crcs[i] != gcrc:
                result["bad_blocks"].append({
                    "index": i,
                    "offset": off,
                    "size": min(block_size, n - off),
                    "rom_crc32": f"{block_crcs[i]:08X}",
                    "good_crc32": f"{gcrc:08X}",
                    "severity": "defective" if i < good_blocks else "extra",
                })
    result["good_blocks"] = blocks - len(result["bad_blocks"])

    # Overall compliance: size passes AND no bad blocks (if good reference given) OR just size passes
    compliance = (result["size_ok"] is True)
    if good is not None:
        compliance = compliance and (len(result["bad_blocks"]) == 0)
    result["compliance"] = compliance
    return result
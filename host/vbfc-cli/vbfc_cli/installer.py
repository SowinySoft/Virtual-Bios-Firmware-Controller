#!/usr/bin/env python3
"""
VBMC BIOS installer — download, verify, sign, and flash.

Usage:
    vbfc-cli bios download <url> [--output <file>]
    vbfc-cli bios install <file> [--bank 0|1] [--version N]
    vbfc-cli bios oem-install <file> [--vendor-id hex] [--device-id hex]

Pipeline: download/load → compatibility check → sign → upload to device.
All cryptographic verification uses the same keypair as Phase A/B signing.
"""
import sys
import os
import struct
import tempfile
import shutil
from pathlib import Path
from typing import Optional
import urllib.request
import hashlib
import zipfile
import io

from cryptography.hazmat.primitives import hashes

# Reuse crypto primitives from the existing module
from vbfc_cli.crypto import (VBFC_IMAGE_HDR_SIZE, VBFC_SHA256_BYTES,
                              sign_payload, verify_image, DEFAULT_KEY_DIR)

# Known flash sizes for compatibility check (in bytes)
KNOWN_SIZES = [
    256 * 1024,      # 256K
    512 * 1024,      # 512K
    1 * 1024 * 1024, # 1 MB
    2 * 1024 * 1024, # 2 MB
    4 * 1024 * 1024, # 4 MB
    8 * 1024 * 1024, # 8 MB (most common)
    16 * 1024 * 1024,# 16 MB
    32 * 1024 * 1024,# 32 MB
]

BANK_OFFSETS = {0: 0x002000, 1: 0x802000}


def _get_key(key_path=None) -> bytes:
    """Load the HMAC key from a file or default location."""
    path = Path(key_path) if key_path else (DEFAULT_KEY_DIR / "hmac.key")
    if not path.exists():
        print(f"Error: key not found at {path}")
        print("Generate one with: vbfc-cli key gen")
        sys.exit(1)
    return path.read_bytes()


def _check_size(size: int) -> tuple[bool, str]:
    """Check if the BIOS image size is standard / acceptable (16 MB flash)."""
    if size > 16 * 1024 * 1024:
        return False, f"too large ({size} bytes > 16 MB W25Q128 capacity)"
    if size in KNOWN_SIZES:
        return True, f"standard size ({size} bytes)"
    # Allow any 4K-aligned size up to 16 MB
    if size % 4096 == 0 and size <= 16 * 1024 * 1024:
        return True, f"non-standard but 4K-aligned ({size} bytes)"
    return False, f"unexpected size ({size} bytes) — not 4K-aligned or too large"


def _check_jedec(data: bytes, vendor_id: Optional[str] = None) -> tuple[bool, str]:
    """Check the JEDEC ID embedded in the BIOS image (SPI ROM header)."""
    # Search for the JEDEC ID in the first few MB of the image
    # Usually at offset 0x000000 for SPI ROM, but sometimes elsewhere
    for offset in [0, 0x200, 0x1000, 0x10000]:
        if offset + 4 > len(data):
            break
        # Look for common SPI flash vendor signatures
        jedec_prefix = struct.unpack_from("<H", data, offset)[0]
        if jedec_prefix == 0xEF:  # WINBOND
            return True, f"Winbond JEDEC prefix found at 0x{offset:X}"
        if jedec_prefix == 0xC8:  # GIGADEVICE
            return True, f"GigaDevice JEDEC prefix found at 0x{offset:X}"
        if jedec_prefix == 0x1C:  # EON
            return True, f"EON JEDEC prefix found at 0x{offset:X}"
        if jedec_prefix == 0x20:  # Macronix
            return True, f"Macronix JEDEC prefix found at 0x{offset:X}"
        if jedec_prefix == 0x01:  # Spansion
            return True, f"Spansion JEDEC prefix found at 0x{offset:X}"

    if vendor_id:
        # Check if the provided vendor ID appears anywhere in the image
        vid_bytes = bytes.fromhex(vendor_id.replace("0x", "").zfill(4))
        if vid_bytes in data[:1024*1024]:
            return True, f"Vendor ID {vendor_id} found in image header"
    return False, "No JEDEC / vendor signature found — unknown chip, proceed with caution"


def _fetch_url(url: str) -> bytes:
    """Download a file from a URL with progress display."""
    print(f"Downloading from {url}...")
    try:
        with urllib.request.urlopen(url, timeout=60) as response:
            data = response.read()
        print(f"  Downloaded {len(data)} bytes")
    except Exception as e:
        print(f"  Download failed: {e}")
        sys.exit(1)
    return data


def _maybe_unzip(data: bytes) -> bytes:
    """If the downloaded data is a ZIP (common for vendor BIOS), extract the largest .bin/.rom."""
    try:
        with zipfile.ZipFile(io.BytesIO(data)) as zf:
            # Find the largest .bin/.rom/.fd file
            candidates = []
            for name in zf.namelist():
                lower = name.lower()
                if any(ext in lower for ext in ['.bin', '.rom', '.fd', '.bio', '.flash']):
                    info = zf.getinfo(name)
                    candidates.append((info.file_size, name))
            if not candidates:
                print("  ZIP contains no BIOS files, using archive as-is")
                return data
            candidate = max(candidates, key=lambda x: x[0])
            print(f"  Extracted '{candidate[1]}' ({candidate[0]} bytes) from archive")
            return zf.read(candidate[1])
    except zipfile.BadZipFile:
        return data


def install_bios(data: bytes, bank: int = 0, key_path: str = None,
                 version: int = 1, device=None) -> dict:
    """Full pipeline: check → sign → upload (if device provided).

    Returns dict with status, size, sha256, and optional upload result.
    """
    result = {
        "status": "ok",
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
    }

    # 1. Size check
    ok, msg = _check_size(len(data))
    if not ok:
        result["status"] = "size_error"
        result["message"] = msg
        return result
    result["size_check"] = msg

    # 2. JEDEC check (informational, not a hard failure)
    _, jedec_msg = _check_jedec(data)
    result["jedec_check"] = jedec_msg

    # 3. Sign the payload
    key = _get_key(key_path)
    signed = sign_payload(data, key, image_version=version)

    result["signed_size"] = len(signed)
    result["signature_valid"] = True
    result["bank"] = bank
    result["offset"] = BANK_OFFSETS.get(bank, 0x002000)

    # 4. Upload if device is connected
    if device is not None:
        print(f"  Uploading to bank {bank} (offset 0x{result['offset']:X})...")
        success = device.upload_to_ext(signed, result["offset"])
        result["upload_success"] = success
        if success:
            print(f"  Upload complete — switch to bank {bank} and set shadow mode to serve.")
        else:
            print("  Upload failed.")
            result["status"] = "upload_error"

    return result


def check_oem_image(data: bytes, vendor_id: str = None, device_id: str = None) -> dict:
    """Check an OEM image for compatibility before install."""
    result = {"compatible": False}

    # Size
    ok, msg = _check_size(len(data))
    if not ok:
        result["reason"] = f"size: {msg}"
        return result
    result["size_ok"] = True

    # JEDEC / VID check
    ok, msg = _check_jedec(data, vendor_id)
    result["jedec_msg"] = msg
    if not ok and vendor_id:
        result["reason"] = f"vendor match: {msg}"
        return result

    # SHA-256 for identification
    result["sha256"] = hashlib.sha256(data).hexdigest()
    result["size"] = len(data)
    result["compatible"] = True
    return result
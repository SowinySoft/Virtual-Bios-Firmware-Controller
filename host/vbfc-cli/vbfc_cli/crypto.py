#!/usr/bin/env python3
"""
VBFC host-side crypto: key generation, HMAC-SHA256 signing (Phase A), and
Ed25519 signing (Phase B) for signed-image headers.

Dependencies: cryptography >= 42.0

The firmware verifies; the host signs. The device never holds the private key.
"""
import os
import struct
import json
from pathlib import Path
from cryptography.hazmat.primitives import hashes, hmac
from cryptography.hazmat.primitives.kdf.hkdf import HKDF

# Signed-image header constants, must match firmware/image_check.h
VBFC_IMAGE_HDR_MAGIC = 0x56424649  # "VBFI"
VBFC_IMAGE_HDR_VERSION = 1
VBFC_SIG_ALG_HMAC_SHA256 = 0x02
VBFC_SIG_ALG_ED25519 = 0x01

VBFC_SHA256_BYTES = 32
VBFC_HMAC_BYTES = 32
VBFC_PUB_BYTES = 32
VBFC_SIG_BYTES = 64  # Ed25519: 64 bytes; HMAC uses 32 in the 64-byte slot
VBFC_IMAGE_HDR_SIZE = 256

DEFAULT_KEY_DIR = Path.home() / ".vbfc"


def _save_key(key_dir: Path, private_key: bytes, public_key: bytes):
    """Save the keypair to `key_dir`."""
    key_dir.mkdir(parents=True, exist_ok=True)
    (key_dir / "hmac.key").write_bytes(private_key)
    (key_dir / "hmac.key.pub").write_bytes(public_key)
    print(f"Key saved to {key_dir / 'hmac.key'} (32-byte HMAC key)")
    print(f"Public key: {public_key.hex()}")
    print()
    print("# Place this key in vbfc_pubkey.h:")
    array = ", ".join(f"0x{b:02x}" for b in public_key)
    print(f"#define VBFC_HMAC_KEY {{{array}}}")


def generate_keypair(key_dir: Path = DEFAULT_KEY_DIR) -> tuple[bytes, bytes]:
    """Generate a random 32-byte HMAC key (symmetric)."""
    private_key = os.urandom(32)  # 32-byte HMAC key
    # For HMAC the "public key" is just the same secret key.
    public_key = private_key
    _save_key(key_dir, private_key, public_key)
    return private_key, public_key


def _pack_header(
    payload_len: int,
    image_version: int,
    key: bytes,
    sha256: bytes,
    sig_alg: int = VBFC_SIG_ALG_HMAC_SHA256,
) -> bytes:
    """Build a 256-byte signed-image header.

    Args:
        payload_len: length of the payload body in bytes
        image_version: monotonic version number (anti-rollback)
        key: 32-byte HMAC key
        sha256: 32-byte SHA-256 of the payload
        sig_alg: VBFC_SIG_ALG_HMAC_SHA256 or VBFC_SIG_ALG_ED25519

    Returns:
        256-byte header bytes, ready to preprend to the payload.
    """
    hdr = bytearray(VBFC_IMAGE_HDR_SIZE)
    struct.pack_into("<I", hdr, 0, VBFC_IMAGE_HDR_MAGIC)
    hdr[4] = VBFC_IMAGE_HDR_VERSION
    hdr[5] = sig_alg
    struct.pack_into("<H", hdr, 6, image_version)
    struct.pack_into("<I", hdr, 8, payload_len)
    hdr[12:44] = sha256          # sha256
    # pub_key (bytes 44-75) stays zero for Phase A
    # signature slot (bytes 108-139) stays zero for Phase A

    # Compute HMAC-SHA256(key, sha256) and write to bytes 76-107
    h = hmac.HMAC(key, hashes.SHA256())
    h.update(sha256)
    hdr[76:108] = h.finalize()

    return bytes(hdr)


def sign_payload(payload: bytes, key: bytes, image_version: int = 1, sig_alg: int = VBFC_SIG_ALG_HMAC_SHA256) -> bytes:
    """Sign a payload and return the header+payload image.

    Args:
        payload: raw firmware image bytes
        key: 32-byte HMAC key
        image_version: monotonic version number for anti-rollback
        sig_alg: signature algorithm (HMAC_SHA256 or ED25519)

    Returns:
        Signed image bytes (256-byte header + payload).
    """
    import hashlib
    sha256 = hashlib.sha256(payload).digest()
    hdr = _pack_header(len(payload), image_version, key, sha256, sig_alg)
    return hdr + payload


def verify_header(hdr: bytes, key: bytes) -> bool:
    """Verify the signed-image header's HMAC-SHA256."""
    if len(hdr) < VBFC_IMAGE_HDR_SIZE:
        return False
    magic, hdr_version, sig_alg = struct.unpack_from("<IBB", hdr, 0)
    if magic != VBFC_IMAGE_HDR_MAGIC or hdr_version > VBFC_IMAGE_HDR_VERSION:
        return False
    if sig_alg == VBFC_SIG_ALG_HMAC_SHA256:
        sha256 = hdr[12:44]
        stored_hmac = hdr[76:108]
        h = hmac.HMAC(key, hashes.SHA256())
        h.update(sha256)
        expected = h.finalize()
        return stored_hmac == expected
    elif sig_alg == VBFC_SIG_ALG_ED25519:
        # Phase B placeholder
        raise NotImplementedError("Ed25519 verification not implemented yet")
    else:
        return False


def verify_image(signed_image: bytes, key: bytes) -> dict:
    """Verify a signed image file and return header info.

    Returns dict with keys: valid, magic, sig_alg, image_version, payload_len.
    """
    if len(signed_image) < VBFC_IMAGE_HDR_SIZE:
        return {"valid": False, "reason": "too short"}
    hdr = signed_image[:VBFC_IMAGE_HDR_SIZE]
    payload = signed_image[VBFC_IMAGE_HDR_SIZE:]

    magic, hdr_version, sig_alg = struct.unpack_from("<IBB", hdr, 0)
    image_version = struct.unpack_from("<H", hdr, 6)[0]
    payload_len = struct.unpack_from("<I", hdr, 8)[0]
    sha256 = hdr[12:44]

    if magic != VBFC_IMAGE_HDR_MAGIC:
        return {"valid": False, "reason": "bad magic"}

    if payload_len != len(payload):
        return {"valid": False, "reason": "payload_len mismatch"}

    ok = verify_header(hdr, key)
    return {
        "valid": ok,
        "magic": hex(magic),
        "sig_alg": sig_alg,
        "image_version": image_version,
        "payload_len": payload_len,
        "sha256": sha256.hex(),
    }
#ifndef VBFC_ED25519_H
#define VBFC_ED25519_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Ed25519 verify-only (RFC 8032).
 *
 * Verify that `signature` (64 bytes) is a valid Ed25519 signature over
 * the 32-byte SHA-256 payload hash in `msg` against `public_key` (32 bytes).
 * Returns true if the signature is valid.
 *
 * This replaces the HMAC-SHA256 Phase A verify call in image_check.c.
 * The header struct field layout stays the same — pub_key at bytes 44-75,
 * signature at bytes 76-139 in the 256-byte VBFI header.
 */
bool ed25519_verify(const uint8_t msg[32],
                    const uint8_t signature[64],
                    const uint8_t public_key[32]);

#endif /* VBFC_ED25519_H */
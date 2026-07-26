#ifndef VBFC_ED25519_H
#define VBFC_ED25519_H

#include <stdint.h>

/*
 * Ed25519 (RFC 8032) — verify-only.
 *
 * The interposer never signs, only verifies signatures on uploaded image
 * banks. This keeps the device's threat surface minimal (no private key, no
 * signing randomness source) and shrinks the code to just the verification
 * half of the reference implementation.
 *
 * Verify the Ed25519 signature `sig` (64 bytes) over the 32-byte `msg`
 * against the 32-byte `pubkey`. Returns 1 on valid, 0 on invalid.
 *
 * For our use, `msg` is always the 32-byte SHA-256 of a signed-image payload
 * (see image_check.c) — Ed25519 itself hashes `msg` again with SHA-512 as part
 * of the spec, so passing the 32-byte payload hash as the "message" is the
 * standard construction.
 */
int ed25519_verify(const uint8_t sig[64],
                   const uint8_t pubkey[32],
                   const uint8_t msg[32]);

#endif /* VBFC_ED25519_H */

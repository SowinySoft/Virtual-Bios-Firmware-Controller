/*
 * Device-side HMAC symmetric key (Phase A).
 *
 * Compiled into internal-flash .rodata. The host signer must hold the same key
 * to produce a valid image. In Phase B this is replaced by the Ed25519 public
 * key and the HMAC call site in image_check.c switches to ed25519_verify().
 *
 * DO NOT commit the real production key to version control. At release time,
 * generate a random 32-byte key (e.g. `openssl rand -hex 16`), set
 * VBFC_HMAC_KEY below, and re-flash firmware. The `.gitignore` already
 * excludes *.key files; supply the real key at build time via a -D preprocessor
 * define or a separate include file.
 */

/* Placeholder for provisioning — replace with the real key at build time. */
#ifndef VBFC_HMAC_KEY
#define VBFC_HMAC_KEY  {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07, \
                        0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f, \
                        0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17, \
                        0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f}
#endif /* VBFC_HMAC_KEY */
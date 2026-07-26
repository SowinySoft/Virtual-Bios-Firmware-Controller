#ifndef VBFC_SHA256_H
#define VBFC_SHA256_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * Thin SHA-256 + HMAC-SHA256 wrappers over the Pico SDK's hardware-accelerated
 * engine (pico_sha256). Exposed so image_check.c can hash a streamed payload
 * and authenticate it without depending on a software crypto library.
 *
 * Byte order matches Python's hashlib.sha256().digest() (FIPS 180-4 / big-endian
 * digest output) — the verification path uses SHA256_BIG_ENDIAN, so a host
 * computing hashlib.sha256(payload).digest() compares byte-for-byte.
 */

/* One-shot SHA-256 of a contiguous buffer (caller owns data; result -> out[32]). */
void vbfc_sha256(const uint8_t *data, size_t len, uint8_t out[32]);

/*
 * Streaming SHA-256 over a byte buffer that may be assembled from multiple
 * ext-flash reads. Each update() may be called with the data lifetime it owns
 * (the SDK's _blocking variant guarantees the bytes are consumed before return);
 * finish() writes the 32-byte digest and releases the hardware.
 *
 * One engine instance at a time (SDK constraint); do not nest streaming calls.
 */
typedef struct {
    void *_sdk;   /* opaque pico_sha256_state_t* */
} vbfc_sha256_stream_t;

void vbfc_sha256_stream_start(vbfc_sha256_stream_t *s);
void vbfc_sha256_stream_update(vbfc_sha256_stream_t *s,
                               const uint8_t *data, size_t len);
void vbfc_sha256_stream_finish(vbfc_sha256_stream_t *s, uint8_t out[32]);

/* RFC 2104 HMAC-SHA256. Key is hashed/lpadded to one block (64 B) per spec. */
void vbfc_hmac_sha256(const uint8_t *key, size_t key_len,
                      const uint8_t *msg, size_t msg_len,
                      uint8_t out[32]);

/* Access the compiled-in device HMAC key (Phase A). Phase B replaces this
 * with an Ed25519 public key — the callers stay the same, so the key is
 * always exposed through an accessor, never a global. */
const uint8_t *vbfc_device_hmac_key(void);
size_t vbfc_device_hmac_key_len(void);

#endif /* VBFC_SHA256_H */

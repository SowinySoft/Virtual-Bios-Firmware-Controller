#ifndef VBFC_IMAGE_CHECK_H
#define VBFC_IMAGE_CHECK_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Signed-image header + verification layer.
 *
 * Every image the interposer serves to the motherboard from ext-flash must
 * carry a 256-byte signed header at the bank's base offset. Phase A authenticates
 * it with HMAC-SHA256 (symmetric key compiled into firmware); Phase B swaps the
 * verifier to Ed25519 (asymmetric) — the header struct is wire-compatible
 * across the swap, only sig_alg + the verifier call change.
 *
 * On-disk layout (little-endian), all 256 bytes fixed:
 *
 *   0    4   magic            0x56424649 ("VBFI")
 *   4    1   hdr_version      = 1
 *   5    1   sig_alg          0x01 = Ed25519 (Phase B), 0x02 = HMAC-SHA256 (Phase A)
 *   6    2   image_version    u16 monotonic anti-rollback counter
 *   8    4   payload_len      bytes of payload following this header
 *   12   32  sha256           SHA-256(payload) — the hashed content
 *   44   32  pub_key          Ed25519 pubkey (Phase B); reserved/zero in Phase A
 *   76   32  hmac             HMAC-SHA256(key, sha256) — Phase A
 *   108  32  signature        Ed25519(sig) — Phase B; reserved/zero in Phase A
 *   140  116 _pad             zero
 *   --- 256
 *
 * Phase A fills bytes 76-107 (hmac) and leaves 44-75 + 108-139 zero.
 * Phase B fills 44-75 (pub_key) and 108-139 (signature), leaves 76-107 zero.
 * Either phase reads magic/hdr_version/sig_alg/payload_len/sha256 identically.
 */

#define VBFC_IMAGE_HDR_MAGIC    0x56424649u   /* "VBFI" little-endian: 49 46 42 56 */
#define VBFC_IMAGE_HDR_VERSION  1u
#define VBFC_SIG_ALG_ED25519    0x01u
#define VBFC_SIG_ALG_HMAC_SHA256 0x02u

#define VBFC_SHA256_BYTES  32u
#define VBFC_PUB_BYTES     32u
#define VBFC_HMAC_BYTES    32u
#define VBFC_SIG_BYTES     32u   /* Phase B Ed25519 is 64; the slot reserves 32
                                   now and grows when Phase B lands */

#define VBFC_IMAGE_HDR_SIZE 256u

/* Largest payload we can hash in one streaming pass is bounded by ext-flash,
 * not SRAM — the verifier streams from flash in CHUNK-sized reads. */
#define VBFC_VERIFY_CHUNK  256u  /* bytes per ext-flash read while hashing */

/* Persistent anti-rollback counter: two redundant copies in sector 0 tail,
 * at an offset past the shadow-map mirror (~80 B) + 32 B gap. Must be 4-aligned
 * and inside EXT_OFF_MAP_META's 4 KB sector. */
#define VBFC_VERSION_OFFSET  0x000080u   /* within sector 0, after the map */

typedef struct __attribute__((packed)) {
    uint32_t magic;                                 /*  0 */
    uint8_t  hdr_version;                           /*  4 */
    uint8_t  sig_alg;                               /*  5 */
    uint16_t image_version;                         /*  6 */
    uint32_t payload_len;                           /*  8 */
    uint8_t  sha256[VBFC_SHA256_BYTES];             /* 12 */
    uint8_t  pub_key[VBFC_PUB_BYTES];               /* 44 */
    uint8_t  hmac[VBFC_HMAC_BYTES];                 /* 76 */
    uint8_t  signature[VBFC_SIG_BYTES];             /* 108 */
    uint8_t  _pad[VBFC_IMAGE_HDR_SIZE - 140];       /* 140..255 */
} vbfc_image_header_t;

_Static_assert(sizeof(vbfc_image_header_t) == VBFC_IMAGE_HDR_SIZE,
               "header must pack to exactly 256 bytes");

/* Verify the signed-image header at `bank_offset` in ext-flash. Reads the
 * 256-byte header, recomputes SHA-256(payload) by streaming the payload
 * bytes, compares to hdr.sha256, then authenticates per hdr.sig_alg
 * (HMAC-SHA256 in Phase A). On success returns true and copies the 32-byte
 * payload hash into `out_hash` (may be NULL). On any failure returns false
 * and the caller must NOT serve the bank. */
bool image_check_verify_header(uint32_t bank_offset, uint8_t *out_hash);

/* Persist the accepted image version (anti-rollback) to ext-flash sector 0.
 * Writes two redundant copies; idempotent. Called after a verified ULOAD. */
bool image_check_accept_version(uint16_t new_version);

/* Read the highest accepted image version from persistent storage. Returns 0
 * if the persistent slot is uninitialized/corrupt (which is safe: any
 * version >= 1 is then accepted). */
uint16_t image_check_accepted_version(void);

/* Boot-time sweep: walk the shadow map, verify every EXT-mapped bank, and
 * populate the in-RAM serve cache so load_tx_byte() serves without per-
 * transaction verification. Banks that fail verification are flagged so the
 * arbiter drops to pass-through on first access. Call after ext_flash_init()
 * and shadow_map_load(). */
void image_check_verify_on_boot(void);

/* Serve-path cache probe. Returns true if `bank_offset` has a cached, verified
 * signature from this boot (i.e. it's safe to serve). Used by the arbiter's
 * load_tx_byte() EXT branch — O(1), no crypto on the hot path. */
bool image_check_serve_allowed(uint32_t bank_offset);

/* Called by up_done() after a verified ULOAD to populate the serve cache. */
void image_check_populate_cache(uint32_t bank_offset);

#endif /* VBFC_IMAGE_CHECK_H */

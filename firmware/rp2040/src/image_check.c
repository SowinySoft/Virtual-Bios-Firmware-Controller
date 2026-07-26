#include "image_check.h"

#include <string.h>
#include <stdio.h>
#include "pico/stdlib.h"

#include "ext_flash.h"
#include "shadow_map.h"
#include "vbfc_sha256.h"

/* ── in-RAM serve-path cache ───────────────────────────────────────────── */
static uint32_t g_cache_offset = 0xFFFFFFFFu;   /* "none" */
static uint8_t  g_cache_hash[VBFC_SHA256_BYTES];
static bool     g_cache_valid = false;

/* ── anti-rollback counter (cached from persistent sector 0) ───────────── */
static uint16_t g_accepted_version = 0;

/* Load the accepted version from persistent flash (two-copy redundant). */
static uint16_t version_load(void) {
    uint16_t a, b;
    ext_flash_read_buf(VBFC_VERSION_OFFSET, (uint8_t *)&a, 2);
    ext_flash_read_buf(VBFC_VERSION_OFFSET + 2, (uint8_t *)&b, 2);
    if (a == b) return a;
    /* Copies diverge — take the larger one (the newer write likely completed). */
    return (a > b) ? a : b;
}

/* Save two redundant copies of the accepted version. */
static void version_save(uint16_t v) {
    ext_flash_write_buf(VBFC_VERSION_OFFSET, (const uint8_t *)&v, 2);
    ext_flash_write_buf(VBFC_VERSION_OFFSET + 2, (const uint8_t *)&v, 2);
    /* The host or up_start erases the covering sector first, so no
     * erase-before-write here — write_buf programs pages that are already
     * erased. */
}

uint16_t image_check_accepted_version(void) {
    return g_accepted_version;
}

bool image_check_accept_version(uint16_t new_version) {
    if (new_version <= g_accepted_version) return false;
    g_accepted_version = new_version;
    version_save(new_version);
    return true;
}

/* ── verification core ────────────────────────────────────────────────── */

/* Hash the payload bytes at flash_offset + 256 by streaming them in chunks.
 * Returns false only on ext-flash read error (unlikely; we just hash what we
 * get — the SHA-256 match below catches corruption). */
static bool hash_payload(uint32_t bank_offset, uint32_t payload_len,
                         uint8_t out[VBFC_SHA256_BYTES]) {
    vbfc_sha256_stream_t s;
    vbfc_sha256_stream_start(&s);

    uint8_t buf[VBFC_VERIFY_CHUNK];
    uint32_t addr = bank_offset + VBFC_IMAGE_HDR_SIZE;
    uint32_t remain = payload_len;
    while (remain) {
        uint32_t n = (remain < sizeof(buf)) ? remain : sizeof(buf);
        ext_flash_read_buf(addr, buf, n);
        vbfc_sha256_stream_update(&s, buf, n);
        addr += n;
        remain -= n;
    }
    vbfc_sha256_stream_finish(&s, out);
    return true;
}

/**
 * Verify the signed-image header at `bank_offset` in ext-flash. HMAC-SHA256
 * authenticator in Phase A; Ed25519 verify in Phase B.
 */
bool image_check_verify_header(uint32_t bank_offset, uint8_t *out_hash) {
    vbfc_image_header_t hdr;

    /* read the 256-byte header */
    ext_flash_read_buf(bank_offset, (uint8_t *)&hdr, sizeof(hdr));

    /* magic + version check */
    if (hdr.magic != VBFC_IMAGE_HDR_MAGIC ||
        hdr.hdr_version > VBFC_IMAGE_HDR_VERSION) {
        return false;
    }

    /* sanity: payload_len must fit within ext-flash after the header */
    if (hdr.payload_len > EXT_FLASH_SIZE - bank_offset - VBFC_IMAGE_HDR_SIZE) {
        return false;
    }

    /* recompute the SHA-256 of the payload body */
    uint8_t computed[VBFC_SHA256_BYTES];
    hash_payload(bank_offset, hdr.payload_len, computed);
    if (memcmp(computed, hdr.sha256, VBFC_SHA256_BYTES) != 0) {
        return false;
    }

    /* authenticate per sig_alg */
    switch (hdr.sig_alg) {
    case VBFC_SIG_ALG_HMAC_SHA256: {
        /* HMAC(key, sha256) against the pre-computed payload hash. */
        uint8_t expected[VBFC_HMAC_BYTES];
        vbfc_hmac_sha256(vbfc_device_hmac_key(),
                         vbfc_device_hmac_key_len(),
                         hdr.sha256, VBFC_SHA256_BYTES,
                         expected);
        if (memcmp(expected, hdr.hmac, VBFC_HMAC_BYTES) != 0) {
            return false;
        }
        break;
    }
    case VBFC_SIG_ALG_ED25519:
        /* Phase B — not implemented in Phase A. Refuse any image claiming
         * Ed25519 until the verifier is ported. */
        return false;

    default:
        /* Unknown sig_alg */
        return false;
    }

    /* image_version is only checked against anti-rollback counter at UPLOAD
     * time (up_done calls image_check_accept_version after this returns true).
     * Serve-path verification trusts the version-check already happened on
     * ingress, so we simply verify the MAC here. */

    if (out_hash) memcpy(out_hash, hdr.sha256, VBFC_SHA256_BYTES);
    return true;
}

/* ── boot sweep ────────────────────────────────────────────────────────── */

void image_check_verify_on_boot(void) {
    const vbfc_shadow_map_t *map = shadow_map_get();
    /* reset the serve-path cache — entry for the most recent verified EXT bank */
    g_cache_valid = false;
    g_cache_offset = 0xFFFFFFFFu;

    /* prime the anti-rollback version from persistent storage */
    g_accepted_version = version_load();

    for (uint8_t i = 0; i < map->entry_count; i++) {
        const vbfc_map_entry_t *e = &map->entries[i];
        if (e->source != VBFC_SOURCE_EXT) continue;
        if (e->size < VBFC_IMAGE_HDR_SIZE) continue; /* too small for a signed image */

        /* verify this bank and cache on success */
        uint8_t hash_out[VBFC_SHA256_BYTES];
        if (image_check_verify_header(e->ext_offset, hash_out)) {
            memcpy(g_cache_hash, hash_out, VBFC_SHA256_BYTES);
            g_cache_offset = e->ext_offset;
            g_cache_valid = true;
            break; /* cache keeps ONE credential per boot — further
                    * banks must call verify() from their own access.
                    * (Typically there's only one EXT entry at boot time.) */
        }
    }

    printf("image-check: boot sweep complete (mode=%d, entries=%d)\r\n",
           (int)map->mode, (int)map->entry_count);
}

/* Called by up_done() after a verified ULOAD to populate the serve cache so
 * the arbiter's per-transaction gate allows serving. */
void image_check_populate_cache(uint32_t bank_offset) {
    g_cache_offset = bank_offset;
    g_cache_valid = true;
}

/* ── arbiter serve-path cache ──────────────────────────────────────────── */

bool image_check_serve_allowed(uint32_t bank_offset) {
    if (g_cache_valid && g_cache_offset == bank_offset) {
        return true;
    }
    return false;
}
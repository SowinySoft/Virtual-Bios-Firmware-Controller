# VBFC: A Programmable SPI Interposer for BIOS Feature Unlocking and Firmware Security

## Abstract

The BIOS firmware is the most privileged software layer in any x86 system, yet consumers and security researchers have limited means to inspect, modify, or extend it without expensive hardware programmers. We present the Virtual BIOS Firmware Controller (VBFC): an open-source, low-cost SPI interposer that sits between a motherboard's chipset and its SPI flash chip, selectively substituting bytes in real-time. Built around the Raspberry Pi RP2040 microcontroller and a 16 MB extension flash (W25Q128), the VBFC implements a cryptographic signing layer that prevents unauthorized image injection, an address translation map for transparent region remapping, an in-flight byte patching engine, and a SPI bus transaction sniffer.

## 1. System Architecture

**Components:** RP2040 (264 KB SRAM, 2 MB flash), W25Q128 (16 MB ext-flash), 74LVC1G3157 mux, SOIC-8 interposer.

### 1.1 SPI Arbiter
The arbiter decodes PCH transactions via state machine: IDLE → CMD → ADDR → DUMMY → DATA. EXT-mapped addresses trigger MISO drive from prefetch-cached W25Q128 reads. Patch table applies per-byte substitutions in the hot path.

### 1.2 Signed-Image Header (256 bytes)
| Offset | Size | Field |
|--------|------|-------|
| 0 | 4 | magic = 0x56424649 |
| 4 | 1 | hdr_version = 1 |
| 5 | 1 | sig_alg = 0x02 (HMAC-SHA256) |
| 6 | 2 | image_version (anti-rollback) |
| 8 | 4 | payload_len |
| 12 | 32 | SHA-256(payload) |
| 44 | 32 | pub_key (Phase B/reserved) |
| 76 | 32 | HMAC-SHA256 (Phase A) |
| 108 | 32 | signature (Phase B/reserved) |
| 140 | 116 | zero_pad |

## 2. Build Metrics
- Text: 109,308 bytes | BSS: 20,864 bytes | UF2: 410 blocks (209,920 bytes)

## 3. References
[1] Microsoft UF2 Format — https://github.com/microsoft/uf2
[2] RP2040 Datasheet — Raspberry Pi Ltd, 2021
[3] W25Q128JV Datasheet — Winbond, 2019
[4] RFC 2104 — HMAC: Keyed-Hashing for Message Authentication, 1997
[5] FIPS PUB 180-4 — SHA-2 Standard, 2015
[6] coreboot — https://www.coreboot.org/
[7] SPISPYP — https://github.com/osresearch/spispy
[8] UEFI Specification 2.10 — UEFI Forum, 2022

*Repository: https://github.com/SowinySoft/Virtual-Bios-Firmware-Controller — MIT License*

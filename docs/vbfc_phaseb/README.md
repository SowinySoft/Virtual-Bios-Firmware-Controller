# VBFC Phase B — ECDSA Key Rotation

## Purpose
Explore asymmetric key rotation (Phase B) to enable:
- Multi-party authorization (vendor + enterprise + user keys)
- Key rotation without physical VBFC replacement (signed key manifests)
- Integration with certificate transparency logs for auditability

## Roadmap
1. Define ECDSA-P256 key manifest schema
2. Implement host-tool signing extension
3. Design secure OTP/element storage for Phase-B keys
4. Add key rotation workflow to `vbfc-host` CLI
5. Integrate with certificate transparency infrastructure

## Links
- Paper Section 9.2: Future Work — Asymmetric Key Rotation (Phase B)
- Hardware: RP2040 ECC accelerator + external secure element (ATECC608A)
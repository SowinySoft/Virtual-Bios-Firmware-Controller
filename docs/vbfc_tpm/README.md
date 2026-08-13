# VBFC TPM/Measured Boot Integration — Case Study

## Purpose
Extend VBFC to include PCR measurements (TPM 2.0 TPM2_Extend or IMA hooks) so the active firmware image and patch set are included in the measured-boot chain transparently.

## Roadmap
1. Design PCR bank selection (e.g., PCR 0–7 or 12–15) for firmware image hash
2. Implement RP2040 host-tool `vbfc tpm-extend` command to extend selected PCR with SHA-256 of active image + patch table
3. Add IMA appraisal hooks (on-board, if OS is Linux) for early-measurement reporting
4. Produce measured-boot report format (EBR / EFI) compatible with UEFI firmware manager
5. Integrate with platform firmware (TianoCore) to expose measured boot state to OS loader

## Links
- Paper Section 9.5: Future Work — Integration with Firmware TPM/Measured Boot
- Reference: TPM2.0 Spec PCR commands, IMA extended verifies

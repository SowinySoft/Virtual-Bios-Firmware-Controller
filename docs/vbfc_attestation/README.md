# VBFC Remote Attestation — Case Study

## Purpose
Extend the VBFC to produce signed attestation reports (via TPM-2.0 PCR measurements) to enable remote verification of device integrity.

## Roadmap
1. Derive attestation key from RP2040 OTP master key
2. Produce quote structure compatible with TCG TPM 2.0 or AMD SEV-SNP
3. Include PCR measurements of firmware image + shadow map + patch table
4. Integrate with remote verification services (e.g., Charter, Azure Attestation)
5. Add CLI `vbfc attest` command for challenge-response

## Links
- Paper Section 9.3: Future Work — Remote Attestation
- TCG TPM 2.0 Spec, Quote Structure (TPM2_GetCapability PCRs)

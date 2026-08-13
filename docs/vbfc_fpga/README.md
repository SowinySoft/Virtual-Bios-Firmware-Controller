# VBFC FPGA Arbiter — Case Study

## Purpose
Explore an FPGA-based (Lattice ECP5 / Xilinx Artix-7) replacement for the RP2040 PIO arbiter to enable:
- Sub-nanosecond arbitration latency
- Full QSPI / Octal-SPI (OPI) support (8+ data lines)
- Hardened crypto cores (AES-GCM, ECDSA, KMAC) with side-channel countermeasures
- Multiple independent SPI channels (multi-flash platforms)
- Formal verification of the arbiter state machine (Verilog + JasperGold/CoSA)

## Roadmap
1. Define SPI arbiter FSM in Verilog
2. Simulate with ModelSim / Vivado simulator
3. Synthesize on target FPGA
4. Integrate with RP2040 / host toolchain bridge
5. Formal property checking (clock-domain crossing, safe states)

## Links
- Paper Section 9.1: Future Work — FPGA-Based Arbiter (VBFC-FPGA)
- Target platforms: enterprise/server where SPI > 100 MHz
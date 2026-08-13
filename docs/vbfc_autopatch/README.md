# VBFC Automated Patch Discovery — Case Study

## Purpose
Integrate with firmware analysis frameworks (UEFITool, UEFIRE, IDA Pro plugins) to automatically identify patchable regions (hidden menus, feature flags, ME policy bits) and generate patch tables.

## Roadmap
1. Define patch table format (offset, size, replacement bytes, authorization level)
2. Build UEFITool plugin skeleton for VBFC region detection
3. Outline UEFIRE rule-set for SPI flash post-IBB volume parsing
4. Sketch IDA Pro Python plugin for auto-discovery of MMIO/register patches
5. Conceptualize "patch marketplace": signed, community-contributed patches with reproducibility hashes

## Links
- Paper Section 9.4: Future Work — Automated Patch Discovery
- Reference: UEFITool source, UEFIRE rule format spec

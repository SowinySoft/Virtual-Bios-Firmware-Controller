# VBFC Full-Page Academic Paper

This directory contains the **full-page (single-column, A4)** version of the VBFC academic paper, formatted for clarity and printability with all diagrams, figures, and tables rendered at full width.

## Paper Overview

**Title:** *VBFC: A Programmable SPI Interposer for BIOS Feature Unlocking and Firmware Security*

**Format:** Article class, 11pt, A4, single-column, generous margins (25mm)

**Target:** Conference/journal submission, technical report, or archival documentation

## Directory Structure

```
vbfc_paper_full/
├── vbfc_paper_full.tex          # Main LaTeX file
├── vbfc_paper_full.bib          # Bibliography (18 references)
├── build.sh                     # Linux/macOS build script
├── build.bat                    # Windows build script
├── sections_full/               # 10 main sections
│   ├── 01_introduction.tex
│   ├── 02_background_related_work.tex
│   ├── 03_threat_model.tex
│   ├── 04_system_architecture.tex
│   ├── 05_spi_arbiter_design.tex
│   ├── 06_security_architecture.tex
│   ├── 07_implementation.tex
│   ├── 08_evaluation.tex
│   ├── 09_discussion_future_work.tex
│   └── 10_conclusion.tex
├── appendix_full/               # 5 appendices
│   ├── a_signed_image_header.tex
│   ├── b_shadow_map_format.tex
│   ├── c_host_toolchain_api.tex
│   ├── d_hardware_bom_schematics.tex
│   └── e_real_hardware_test_phase.tex
└── Figures (PNG):
    ├── fig1_architecture.png    # System architecture block diagram
    ├── fig2_state_machine.png   # SPI arbiter FSM
    ├── fig3_shadow_map_header.png
    └── fig4_timing_analysis.png
```

## Building the Paper

### Prerequisites

- **TeX Live 2024+** or **MiKTeX** (full installation recommended)
- Required packages (auto-installed by `tlmgr` if using TeX Live):
  ```
  ieeetran biblatex biber pgfplots tikz siunitx booktabs
  caption subcaption listings xcolor hyperref cleveref
  algorithm algorithmicx algpseudocode glossaries acronym
  enumitem microtype geometry appendix longtable multirow array
  ```

### Linux/macOS (bash)

```bash
cd docs/vbfc_paper_full
chmod +x build.sh
./build.sh
```

### Windows (Command Prompt / PowerShell)

```cmd
cd docs\vbfc_paper_full
build.bat
```

### Manual Build (4-pass)

```bash
pdflatex vbfc_paper_full.tex
biber vbfc_paper_full
pdflatex vbfc_paper_full.tex
pdflatex vbfc_paper_full.tex
```

## Output

- **`vbfc_paper_full.pdf`** — ~15-20 pages, print-ready
- All cross-references, citations, bibliography, and bookmarks resolved
- Hyperlinked table of contents, figures, tables, and equations

## Key Differences from Journal Version

| Aspect | Journal (IEEEtran) | Full-Page (this version) |
|--------|-------------------|-------------------------|
| Columns | 2-column | 1-column |
| Page size | US Letter | A4 |
| Font size | 10pt | 11pt |
| Margins | ~19mm | 25mm |
| Figures | Scaled to column | Full width |
| Code listings | Narrow | Full width readable |
| Equations | Compact | Spacious |

## Figures

The paper includes **4 existing PNG figures** (from `paper_assets/`) and **2 native TikZ diagrams** rendered inline:

1. **System Architecture** (TikZ + PNG) — Block diagram with RP2040 internals
2. **SPI Arbiter FSM** (TikZ + PNG) — State machine with patch cache annotation
3. **Signed Header Format** (PNG) — 256-byte header field map
4. **SPI Timing Analysis** (PNG) — Latency bar chart

## Appendices

| Appendix | Content |
|----------|---------|
| A | Signed Image Header (256 bytes, field-by-field) |
| B | Shadow Map Format (header, entries, CRC, atomic updates) |
| C | Host Toolchain API (`vbfc-host` Python classes) |
| D | Hardware BOM, Schematics (4 sheets), PCB Constraints |
| E | Real Hardware Test Phase (Gigabyte G41, 10 phases) |

## Copyright

© 2026 SowinySoft (Mr. Said Sowiny)

- **Email:** SowinySoft@gmail.com
- **WhatsApp:** +201100162814
- **X/Twitter:** https://x.com/SowinySoft

Licensed under MIT License. See repository for details.

## Related

- **Journal version:** `../vbfc_paper/` (IEEEtran 2-column)
- **Test phase docs:** `../VBFC-Real-Hardware-Test-Phase.md` (Markdown)
- **Source repository:** https://github.com/SowinySoft/Virtual-Bios-Firmware-Controller
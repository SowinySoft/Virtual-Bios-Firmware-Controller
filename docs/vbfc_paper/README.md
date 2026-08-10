# VBFC Academic Paper

This directory contains the complete LaTeX source for the academic paper:

**"VBFC: A Programmable SPI Interposer for BIOS Feature Unlocking and Firmware Security"**

## Structure

```
vbfc_paper/
├── vbfc_paper.tex           # Main LaTeX document
├── vbfc_paper.bib           # Bibliography
├── build.sh                 # Build script (Linux/macOS)
├── build.bat                # Build script (Windows)
├── sections/                # Paper sections
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
├── appendix/                # Appendices
│   ├── a_signed_image_header.tex
│   ├── b_shadow_map_format.tex
│   ├── c_host_toolchain_api.tex
│   └── d_hardware_bom_schematics.tex
└── figures/                 # Figures (copied from paper_assets/)
    ├── fig1_architecture.png
    ├── fig2_state_machine.png
    ├── fig3_shadow_map_header.png
    └── fig4_timing_analysis.png
```

## Building

### Requirements
- TeX Live (2023+) or MiKTeX with packages:
  - `ieeetran` class
  - `biblatex` with `biber` backend
  - `pgfplots`, `tikz` with libraries
  - `algorithm`, `algpseudocode`
  - `siunitx`, `booktabs`, `subcaption`
  - `hyperref`, `cleveref`

### Linux/macOS
```bash
chmod +x build.sh
./build.sh
```

### Windows
```cmd
build.bat
```

### Manual Build
```bash
pdflatex vbfc_paper.tex
biber vbfc_paper
pdflatex vbfc_paper.tex
pdflatex vbfc_paper.tex
```

## Output
- `vbfc_paper.pdf` — Print-ready PDF (IEEE/ACM journal format, 2-column, 10pt)

## Figures
The paper references 4 figures from the existing `paper_assets/` directory. They are copied to the build directory by the build script. The figures are:
1. **fig1_architecture.png** — System architecture block diagram
2. **fig2_state_machine.png** — SPI arbiter state machine
3. **fig3_shadow_map_header.png** — Shadow map structure & signed header
4. **fig4_timing_analysis.png** — SPI transaction timing analysis

## Paper Features
- IEEE/ACM journal format (2-column, 10pt)
- Comprehensive bibliography (18 references)
- 4 detailed figures (existing + TikZ diagrams in-text)
- 8 tables
- 4 algorithms/listings
- 4 appendices with full technical specifications
- Print-ready PDF with proper margins, fonts, and layout
- Hyperlinked cross-references, citations, and bookmarks
- Color palette optimized for both screen and print

## License
MIT License — see repository for details.
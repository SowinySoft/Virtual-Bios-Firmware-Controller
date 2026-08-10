#!/bin/bash
#==============================================================================
# Build Script for VBFC Full-Page Academic Paper
#==============================================================================
# This script compiles the LaTeX paper to PDF using pdflatex + biber
# Requirements: TeX Live 2024+ or MiKTeX with packages:
#   ieeetran (for bibliography style), biblatex, biber, pgfplots, tikz,
#   siunitx, booktabs, caption, subcaption, listings, xcolor, hyperref,
#   cleveref, algorithm, algorithmicx, algpseudocode, glossaries, acronym,
#   enumitem, microtype, geometry, appendix, longtable, multirow, array
#==============================================================================

set -euo pipefail

PAPER_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$PAPER_DIR"

MAIN_TEX="vbfc_paper_full.tex"
OUTPUT_PDF="vbfc_paper_full.pdf"

echo "=========================================="
echo "Building VBFC Full-Page Academic Paper"
echo "=========================================="
echo "Working directory: $PAPER_DIR"
echo "Main file: $MAIN_TEX"
echo ""

# Clean previous build artifacts
echo "Cleaning previous build artifacts..."
rm -f *.aux *.bbl *.bcf *.blg *.log *.out *.run.xml *.toc *.lof *.lot *.fls *.fdb_latexmk
rm -f *.synctex.gz *.nav *.snm *.vrb

# Pass 1: pdflatex
echo "Pass 1: pdflatex..."
pdflatex -interaction=nonstopmode -halt-on-error "$MAIN_TEX" 2>&1 | tee build_pass1.log
if [ ${PIPESTATUS[0]} -ne 0 ]; then
    echo "ERROR: Pass 1 failed. Check build_pass1.log"
    exit 1
fi

# Pass 2: biber
echo "Pass 2: biber..."
biber "vbfc_paper_full" 2>&1 | tee build_biber.log
if [ ${PIPESTATUS[0]} -ne 0 ]; then
    echo "ERROR: Biber failed. Check build_biber.log"
    exit 1
fi

# Pass 3: pdflatex
echo "Pass 3: pdflatex..."
pdflatex -interaction=nonstopmode -halt-on-error "$MAIN_TEX" 2>&1 | tee build_pass3.log
if [ ${PIPESTATUS[0]} -ne 0 ]; then
    echo "ERROR: Pass 3 failed. Check build_pass3.log"
    exit 1
fi

# Pass 4: pdflatex (final cross-references)
echo "Pass 4: pdflatex (final)..."
pdflatex -interaction=nonstopmode -halt-on-error "$MAIN_TEX" 2>&1 | tee build_pass4.log
if [ ${PIPESTATUS[0]} -ne 0 ]; then
    echo "ERROR: Pass 4 failed. Check build_pass4.log"
    exit 1
fi

# Verify output
if [ -f "$OUTPUT_PDF" ]; then
    SIZE=$(stat -c%s "$OUTPUT_PDF" 2>/dev/null || stat -f%z "$OUTPUT_PDF" 2>/dev/null)
    PAGES=$(pdfinfo "$OUTPUT_PDF" 2>/dev/null | grep Pages | awk '{print $2}' || echo "?")
    echo ""
    echo "=========================================="
    echo "BUILD SUCCESSFUL"
    echo "=========================================="
    echo "Output: $OUTPUT_PDF"
    echo "Size: $SIZE bytes"
    echo "Pages: $PAGES"
    echo ""
    echo "To view: open $OUTPUT_PDF"
else
    echo "ERROR: PDF not generated"
    exit 1
fi
#!/bin/bash
#==============================================================================
# VBFC Paper Build Script
# Compiles the LaTeX paper to a print-ready PDF
#==============================================================================

set -euo pipefail

PAPER_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$PAPER_DIR"

echo "=========================================="
echo "Building VBFC Academic Paper"
echo "=========================================="

# Check for required tools
for cmd in pdflatex biber; do
    if ! command -v "$cmd" &> /dev/null; then
        echo "ERROR: $cmd not found. Install texlive-full or equivalent."
        exit 1
    fi
done

# Clean previous builds
echo "Cleaning previous build artifacts..."
rm -f *.aux *.bbl *.bcf *.blg *.fdb_latexmk *.fls *.log *.out *.run.xml *.toc *.lof *.lot *.synctex.gz

# Copy figures to build directory if not present
for fig in fig1_architecture.png fig2_state_machine.png fig3_shadow_map_header.png fig4_timing_analysis.png; do
    if [[ ! -f "$fig" ]]; then
        echo "WARNING: Figure $fig not found in build directory"
    fi
done

# Build sequence: pdflatex -> biber -> pdflatex x2
echo ""
echo "Pass 1: pdflatex..."
pdflatex -interaction=nonstopmode -halt-on-error vbfc_paper.tex

echo ""
echo "Pass 2: biber..."
biber vbfc_paper

echo ""
echo "Pass 3: pdflatex..."
pdflatex -interaction=nonstopmode -halt-on-error vbfc_paper.tex

echo ""
echo "Pass 4: pdflatex (final)..."
pdflatex -interaction=nonstopmode -halt-on-error vbfc_paper.tex

# Check output
if [[ -f "vbfc_paper.pdf" ]]; then
    SIZE=$(du -h vbfc_paper.pdf | cut -f1)
    PAGES=$(pdfinfo vbfc_paper.pdf 2>/dev/null | grep Pages | awk '{print $2}' || echo "?")
    echo ""
    echo "=========================================="
    echo "BUILD SUCCESSFUL"
    echo "=========================================="
    echo "Output: vbfc_paper.pdf"
    echo "Size:   $SIZE"
    echo "Pages:  $PAGES"
    echo ""
    
    # Verify PDF integrity
    if pdfinfo vbfc_paper.pdf >/dev/null 2>&1; then
        echo "PDF validation: PASSED"
    else
        echo "PDF validation: FAILED (corrupt?)"
        exit 1
    fi
else
    echo "ERROR: PDF not generated"
    exit 1
fi
@echo off
REM ==============================================================================
REM Build Script for VBFC Full-Page Academic Paper (Windows)
REM ==============================================================================
REM This script compiles the LaTeX paper to PDF using pdflatex + biber
REM Requirements: TeX Live 2024+ or MiKTeX with packages:
REM   ieeetran (for bibliography style), biblatex, biber, pgfplots, tikz,
REM   siunitx, booktabs, caption, subcaption, listings, xcolor, hyperref,
REM   cleveref, algorithm, algorithmicx, algpseudocode, glossaries, acronym,
REM   enumitem, microtype, geometry, appendix, longtable, multirow, array
REM ==============================================================================

setlocal enabledelayedexpansion

set PAPER_DIR=%~dp0
cd /d "%PAPER_DIR%"

set MAIN_TEX=vbfc_paper_full.tex
set OUTPUT_PDF=vbfc_paper_full.pdf

echo ==========================================
echo Building VBFC Full-Page Academic Paper
echo ==========================================
echo Working directory: %PAPER_DIR%
echo Main file: %MAIN_TEX%
echo.

REM Clean previous build artifacts
echo Cleaning previous build artifacts...
del /q *.aux *.bbl *.bcf *.blg *.log *.out *.run.xml *.toc *.lof *.lot *.fls *.fdb_latexmk 2>nul
del /q *.synctex.gz *.nav *.snm *.vrb 2>nul

REM Pass 1: pdflatex
echo Pass 1: pdflatex...
pdflatex -interaction=nonstopmode -halt-on-error %MAIN_TEX% 2>&1 | tee build_pass1.log
if errorlevel 1 (
    echo ERROR: Pass 1 failed. Check build_pass1.log
    exit /b 1
)

REM Pass 2: biber
echo Pass 2: biber...
biber vbfc_paper_full 2>&1 | tee build_biber.log
if errorlevel 1 (
    echo ERROR: Biber failed. Check build_biber.log
    exit /b 1
)

REM Pass 3: pdflatex
echo Pass 3: pdflatex...
pdflatex -interaction=nonstopmode -halt-on-error %MAIN_TEX% 2>&1 | tee build_pass3.log
if errorlevel 1 (
    echo ERROR: Pass 3 failed. Check build_pass3.log
    exit /b 1
)

REM Pass 4: pdflatex (final cross-references)
echo Pass 4: pdflatex (final)...
pdflatex -interaction=nonstopmode -halt-on-error %MAIN_TEX% 2>&1 | tee build_pass4.log
if errorlevel 1 (
    echo ERROR: Pass 4 failed. Check build_pass4.log
    exit /b 1
)

REM Verify output
if exist %OUTPUT_PDF% (
    for %%F in (%OUTPUT_PDF%) do set SIZE=%%~zF
    echo.
    echo ==========================================
    echo BUILD SUCCESSFUL
    echo ==========================================
    echo Output: %OUTPUT_PDF%
    echo Size: %SIZE% bytes
    echo.
    echo To view: start %OUTPUT_PDF%
) else (
    echo ERROR: PDF not generated
    exit /b 1
)
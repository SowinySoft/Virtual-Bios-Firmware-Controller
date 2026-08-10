@echo off
REM ==============================================================================
REM VBFC Paper Build Script (Windows Batch)
REM Compiles the LaTeX paper to a print-ready PDF
REM ==============================================================================

cd /d "%~dp0"

echo ==========================================
echo Building VBFC Academic Paper
echo ==========================================

REM Check for required tools
where pdflatex >nul 2>nul
if errorlevel 1 (
    echo ERROR: pdflatex not found. Install TeX Live or MiKTeX.
    exit /b 1
)

where biber >nul 2>nul
if errorlevel 1 (
    echo ERROR: biber not found. Install TeX Live or MiKTeX.
    exit /b 1
)

REM Clean previous builds
echo Cleaning previous build artifacts...
del /q *.aux *.bbl *.bcf *.blg *.fdb_latexmk *.fls *.log *.out *.run.xml *.toc *.lof *.lot *.synctex.gz 2>nul

REM Build sequence: pdflatex -> biber -> pdflatex x2
echo.
echo Pass 1: pdflatex...
pdflatex -interaction=nonstopmode -halt-on-error vbfc_paper.tex
if errorlevel 1 exit /b 1

echo.
echo Pass 2: biber...
biber vbfc_paper
if errorlevel 1 exit /b 1

echo.
echo Pass 3: pdflatex...
pdflatex -interaction=nonstopmode -halt-on-error vbfc_paper.tex
if errorlevel 1 exit /b 1

echo.
echo Pass 4: pdflatex (final)...
pdflatex -interaction=nonstopmode -halt-on-error vbfc_paper.tex
if errorlevel 1 exit /b 1

REM Check output
if exist vbfc_paper.pdf (
    echo.
    echo ==========================================
    echo BUILD SUCCESSFUL
    echo ==========================================
    echo Output: vbfc_paper.pdf
    for %%F in (vbfc_paper.pdf) do echo Size: %%~zF bytes
    
    REM Try to get page count
    pdfinfo vbfc_paper.pdf 2>nul | findstr /r "Pages" >nul
    if not errorlevel 1 (
        for /f "tokens=2" %%P in ('pdfinfo vbfc_paper.pdf ^| findstr /r "Pages"') do echo Pages: %%P
    )
    
    echo.
    echo PDF validation: PASSED
) else (
    echo ERROR: PDF not generated
    exit /b 1
)
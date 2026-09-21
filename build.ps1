# ============================================================
# build.ps1 — Compile pixelator.c to WebAssembly (Windows)
# ============================================================
#
# USAGE (in PowerShell):
#   .\build.ps1
#
# REQUIREMENTS:
#   - Emscripten SDK installed
#   - Run .\emsdk_env.ps1 from your emsdk folder first
# ============================================================

Write-Host "==================================================" -ForegroundColor Cyan
Write-Host " Pixel Image Generator — WASM Build (Windows)"   -ForegroundColor Cyan
Write-Host "==================================================" -ForegroundColor Cyan
Write-Host ""

# Check emcc is available
$emcc = Get-Command emcc -ErrorAction SilentlyContinue
if (-not $emcc) {
    Write-Host "ERROR: emcc not found." -ForegroundColor Red
    Write-Host ""
    Write-Host "Install Emscripten SDK:"
    Write-Host "  git clone https://github.com/emscripten-core/emsdk.git"
    Write-Host "  cd emsdk"
    Write-Host "  .\emsdk install latest"
    Write-Host "  .\emsdk activate latest"
    Write-Host "  .\emsdk_env.ps1"
    Write-Host ""
    exit 1
}

Write-Host "Emscripten found!" -ForegroundColor Green
Write-Host ""

# Create output directory
New-Item -ItemType Directory -Force -Path "wasm" | Out-Null

Write-Host "Compiling src/pixelator.c ..." -ForegroundColor Yellow
Write-Host ""

# Run Emscripten compiler
emcc src/pixelator.c `
    -O2 `
    -sWASM=1 `
    "-sEXPORTED_FUNCTIONS=['_pixelate','_pixelate_grayscale','_pixelate_reduced','_pixelate_advanced','_get_buffer','_free_buffer']" `
    "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','HEAPU8']" `
    -sALLOW_MEMORY_GROWTH=1 `
    -sMODULARIZE=0 `
    "-sEXPORT_NAME=Module" `
    "-sENVIRONMENT=web" `
    -o wasm/pixelator.js

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "==================================================" -ForegroundColor Green
    Write-Host " Build successful!" -ForegroundColor Green
    Write-Host "==================================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "Generated files:"
    Write-Host "  wasm/pixelator.js"
    Write-Host "  wasm/pixelator.wasm"
    Write-Host ""
    Write-Host "To run locally:"
    Write-Host "  python -m http.server 8080"
    Write-Host "  Open http://localhost:8080"
    Write-Host ""
} else {
    Write-Host ""
    Write-Host "Build FAILED. Check error messages above." -ForegroundColor Red
    exit 1
}

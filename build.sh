#!/bin/bash
# =============================================================================
# build.sh — Compile pixelator.c to WebAssembly using Emscripten
# =============================================================================
#
# USAGE:
#   chmod +x build.sh
#   ./build.sh
#
# REQUIREMENTS:
#   - Emscripten SDK installed and activated
#   - Run `source /path/to/emsdk/emsdk_env.sh` first
#
# OUTPUT:
#   wasm/pixelator.js   — Emscripten-generated JavaScript glue code
#   wasm/pixelator.wasm — Compiled WebAssembly binary
# =============================================================================

set -e  # Exit immediately if any command fails

echo "=================================================="
echo " Pixel Image Generator — WASM Build Script"
echo "=================================================="
echo ""

# Check that emcc is available
if ! command -v emcc &> /dev/null; then
    echo "ERROR: emcc not found."
    echo ""
    echo "Please install and activate Emscripten SDK:"
    echo "  git clone https://github.com/emscripten-core/emsdk.git"
    echo "  cd emsdk"
    echo "  ./emsdk install latest"
    echo "  ./emsdk activate latest"
    echo "  source ./emsdk_env.sh"
    echo ""
    exit 1
fi

echo "✓ Emscripten found: $(emcc --version | head -1)"
echo ""

# Ensure output directory exists
mkdir -p wasm

echo "⚙ Compiling src/pixelator.c → wasm/pixelator.js + wasm/pixelator.wasm"
echo ""

emcc src/pixelator.c \
    -O2 \
    -sWASM=1 \
    -sEXPORTED_FUNCTIONS="['_pixelate','_pixelate_grayscale','_pixelate_reduced','_pixelate_advanced','_get_buffer','_free_buffer']" \
    -sEXPORTED_RUNTIME_METHODS="['ccall','cwrap','HEAPU8']" \
    -sALLOW_MEMORY_GROWTH=1 \
    -sMODULARIZE=0 \
    -sEXPORT_NAME="Module" \
    -sENVIRONMENT='web' \
    -o wasm/pixelator.js

echo ""
echo "=================================================="
echo " ✅ Build successful!"
echo "=================================================="
echo ""
echo " Generated files:"
echo "   wasm/pixelator.js   — $(wc -c < wasm/pixelator.js | tr -d ' ') bytes"
echo "   wasm/pixelator.wasm — $(wc -c < wasm/pixelator.wasm | tr -d ' ') bytes"
echo ""
echo " To run the project locally:"
echo "   python -m http.server 8080"
echo "   → Open http://localhost:8080"
echo ""

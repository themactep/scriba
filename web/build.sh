#!/bin/bash
# Build scriba for WebAssembly
# Auto-downloads Emscripten SDK if not found.
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
EMSDK_DIR="${EMSDK:-$BUILD_DIR/emsdk}"

# --- ensure emcc is available ---
if ! command -v emcc &> /dev/null; then
    # Check if we already have emsdk downloaded but just need to activate it
    if [ -f "$EMSDK_DIR/emsdk_env.sh" ]; then
        echo "Emscripten SDK found at $EMSDK_DIR, activating..."
    else
        echo "Emscripten SDK not found. Downloading..."
        git clone --depth 1 https://github.com/emscripten-core/emsdk.git "$EMSDK_DIR"
    fi

    cd "$EMSDK_DIR"
    ./emsdk install latest
    ./emsdk activate latest
    cd "$SCRIPT_DIR"

    # Source the env script to get emcc on PATH for this shell
    # shellcheck disable=SC1090
    source "$EMSDK_DIR/emsdk_env.sh"

    if ! command -v emcc &> /dev/null; then
        echo "Error: emcc still not available after install."
        echo "Try: source $EMSDK_DIR/emsdk_env.sh && cd web && ./build.sh"
        exit 1
    fi
fi

echo "Using emcc: $(which emcc)"
emcc --version | head -1

# --- WASM build ---
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

emcmake cmake "$SCRIPT_DIR" -DCMAKE_BUILD_TYPE=Release
NPROC=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
emmake make -j"$NPROC" VERBOSE=1

# --- web frontend ---
cd "$SCRIPT_DIR"
[ ! -d node_modules ] && npm install
npm run build

echo ""
echo "Build complete. Output in $SCRIPT_DIR/dist/"
ls -la "$SCRIPT_DIR/dist/"

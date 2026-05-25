#!/bin/bash
# Build scriba for WebAssembly
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

if ! command -v emcc &> /dev/null; then
    echo "Error: emcc not found. Install Emscripten SDK and source emsdk_env.sh"
    echo "  git clone https://github.com/emscripten-core/emsdk.git"
    echo "  cd emsdk && ./emsdk install latest && ./emsdk activate latest"
    echo "  source ./emsdk_env.sh"
    exit 1
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

emcmake cmake "$SCRIPT_DIR" -DCMAKE_BUILD_TYPE=Release
emmake make -j$(nproc) VERBOSE=1

cd "$SCRIPT_DIR"
[ ! -d node_modules ] && npm install
npm run build

echo ""
echo "Build complete. Output in $SCRIPT_DIR/dist/"
ls -la "$SCRIPT_DIR/dist/"

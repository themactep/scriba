#!/bin/bash
# Build scriba for WebAssembly
# Thin wrapper — all logic is in Makefile.
set -e
cd "$(dirname "$0")"
make

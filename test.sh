#!/bin/bash
# Scriba regression test suite
# Tests CLI behavior without requiring hardware.
# Run: make && ./test.sh

set -eu

BIN="${SCRIBA_BIN:-./build/bin/scriba}"
PASS=0
FAIL=0

ok()   { echo "  PASS  $1"; PASS=$((PASS + 1)); }
fail() { echo "  FAIL  $1: $2"; FAIL=$((FAIL + 1)); }

echo "=== Scriba regression tests ==="
echo "binary: $BIN"
echo

# --- build check ---
echo "[build]"
if [ -x "$BIN" ]; then
    ok "binary exists and is executable"
else
    fail "binary not found at $BIN" "run 'make' first"
    exit 1
fi

# --- version ---
echo "[version]"
if "$BIN" --version 2>&1 | grep -q "Thingino Scriba v\."; then
    ok "--version prints version string"
else
    fail "--version" "missing version string"
fi

if "$BIN" -V 2>&1 | grep -q "Thingino Scriba v\."; then
    ok "-V prints version string"
else
    fail "-V" "missing version string"
fi

# --- help ---
echo "[help]"
if "$BIN" -h 2>&1 | grep -q "Usage:"; then
    ok "-h prints usage"
else
    fail "-h" "missing usage"
fi

if "$BIN" --help 2>&1 | grep -q "Usage:"; then
    ok "--help prints usage (via -h default)"
else
    fail "--help" "missing usage"
fi

if "$BIN" -h 2>&1 | grep -q "\-V.*version"; then
    ok "-h lists --version"
else
    fail "-h" "--version not listed"
fi

# --- chip list ---
echo "[chip list]"
if "$BIN" -L 2>&1 | grep -q "SPI NOR Flash Support List"; then
    ok "-L prints NOR list"
else
    fail "-L" "NOR list missing"
fi

if "$BIN" -L 2>&1 | grep -q "SPI NAND Flash Support List"; then
    ok "-L prints NAND list"
else
    fail "-L" "NAND list missing"
fi

# --- no-programmer / chip-detect ---
echo "[detect]"
# -i either detects a chip (programmer connected) or prints error (not connected)
if "$BIN" -i 2>&1 | grep -qE "Detected|not found|No supported"; then
    ok "-i produces detection or error (not crash)"
else
    fail "-i" "unexpected output"
fi

# --- conflicting options ---
echo "[conflicting options]"
if "$BIN" -i -e 2>&1 | grep -qi "conflicting\|only one option"; then
    ok "-i -e detected as conflicting"
else
    fail "-i -e" "no conflict message"
fi

# --- NOR chip table integrity ---
echo "[chip table]"
# verify_chips_sorted() runs at startup on every chip_probe() call.
# Trigger it via -L (which doesn't need a programmer).
# If the table is unsorted, the binary exits with an error before printing the list.
if "$BIN" -L > /dev/null 2>&1; then
    ok "NOR chip table passes sortedness check"
else
    fail "NOR chip table" "verify_chips_sorted() failed"
fi

# --- summary ---
echo
echo "=== Results: $PASS passed, $FAIL failed ==="
[ "$FAIL" -eq 0 ] && exit 0 || exit 1

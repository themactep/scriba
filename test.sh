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

# --- programmer selection ---
echo "[programmer selection]"
if "$BIN" -h 2>&1 | grep -q "ch347"; then
    ok "-h lists the ch347 programmer"
else
    fail "-h" "ch347 not listed"
fi

if "$BIN" -P bogus -i 2>&1 | grep -q "use ch341a, ch347, ezp2019, or auto"; then
    ok "-P rejects an unknown programmer"
else
    fail "-P bogus" "no error listing valid programmers"
fi

# -P ch347 -i either finds the CH347 or names it in the not-found error
if "$BIN" -P ch347 -i 2>&1 | grep -qE "CH347 programmer device not found|Found programmer device: WinChipHead \(WCH\) - CH347"; then
    ok "-P ch347 selects only the CH347 driver"
else
    fail "-P ch347" "unexpected output"
fi

if "$BIN" --spi-speed fast -i 2>&1 | grep -q "Invalid --spi-speed"; then
    ok "--spi-speed rejects a non-numeric value"
else
    fail "--spi-speed fast" "no error"
fi

if "$BIN" --spi-speed 0 -i 2>&1 | grep -q "Invalid --spi-speed"; then
    ok "--spi-speed rejects zero"
else
    fail "--spi-speed 0" "no error"
fi

# --- partition layout (no programmer needed) ---
echo "[layout]"
IMG=$(mktemp)
trap 'rm -f "$IMG"' EXIT
head -c 262144 /dev/zero | tr '\0' '\377' > "$IMG"
printf 'mtdparts=test:64k(boot),64k(env),-(rootfs)' | dd of="$IMG" bs=1 seek=256 conv=notrunc 2>/dev/null
LAYOUT=$("$BIN" --layout "$IMG" 2>&1)
if echo "$LAYOUT" | grep -q "mtdparts (test)" &&
   echo "$LAYOUT" | grep -qE "0x000000-0x00FFFF +64 KB +boot +data" &&
   echo "$LAYOUT" | grep -qE "0x010000-0x01FFFF +64 KB +env +blank" &&
   echo "$LAYOUT" | grep -qE "0x020000-0x03FFFF +128 KB +rootfs +blank"; then
    ok "--layout parses mtdparts and identifies partitions"
else
    fail "--layout" "unexpected output: $LAYOUT"
fi

if "$BIN" --layout /nonexistent/image.bin 2>&1 | grep -q "Couldn't open file"; then
    ok "--layout reports a missing file"
else
    fail "--layout missing file" "no error"
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

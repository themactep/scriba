# Scriba

A command-line flash programming tool for IP camera chips. Scriba reads, writes,
erases, and verifies SPI NOR, SPI NAND, and EEPROM memory using inexpensive USB
programmers. Built for the [Thingino](https://github.com/themactep/thingino-firmware) firmware project.

## Quick Start

```bash
# Build
git clone https://github.com/themactep/scriba.git && cd scriba
make && sudo make install

# Identify the flash chip
scriba -i

# Save a backup
scriba -R backup.bin

# Write new firmware
scriba -W firmware.bin
```

## Supported Programmers

Scriba auto-detects the connected programmer — no configuration needed.

| Programmer | VID:PID | |
|---|---|---|
| **CH341A** (black PCB) | `1a86:5512` | Must be modified for 3.3 V; see [CH341A guide](https://github.com/themactep/thingino-firmware/wiki/CH341A-Programmer) |
| **EZP2019** / **EZP2019+** | `1fc8:310b` / `310c` | Works out of the box |
| **EZP2023** | `1fc8:310d` | |

Force a specific programmer with `-P`:

```bash
scriba -P ch341a -i
scriba -P ezp2019 -i
```

## Build & Install

**Prerequisites:** GCC, libusb-1.0 development files, make

```bash
make              # dynamic linking (needs libusb-1.0 installed)
make static       # static build (bundles libusb)
sudo make install # installs binary + udev rules, reloads udev
```

`make install` copies udev rules for both programmers and reloads them. If your
programmer was already plugged in, unplug and replug it after installation.

## Web Version (WebUSB / WASM)

Scriba also runs in the browser via WebAssembly and WebUSB — no driver
installation needed. Requires a Chromium-based browser (Chrome, Edge, Opera).

### Build

```bash
cd web
npm install
cd build && emcmake cmake .. && emmake make
```

The build outputs `scriba.js` and `scriba.wasm` to `web/public/wasm/`.

**Prerequisites:** Emscripten (`emcc`), CMake, Node.js

### Development

```bash
cd web
npm install
npm run dev      # starts Vite dev server on http://localhost:5173
```

Vite serves static files from `web/` including `web/public/wasm/` where the
Emscripten build places its output. After rebuilding (`emmake make`), just
reload the browser.

### Production

```bash
cd web
npm run build    # outputs to web/dist/
```

Serve the `web/dist/` directory with any static file server.

## Usage

```
scriba [options]

Automation:
  -R <file>    Read chip twice and compare — reliable backup
  -W <file>    Erase + write + verify — safe flash

Operations:
  -i           Read chip ID
  -e           Erase chip
  -r <file>    Read chip to file
  -w <file>    Write file to chip
  -v           Verify after write (use with -w)

Options:
  -a <addr>    Start address (hex or decimal)
  -l <bytes>   Length in bytes
  -L           List all supported chips

SPI NAND:
  -d           Disable on-die ECC
  -o <bytes>   Set OOB size (64–256)
  -I           Ignore ECC errors during read
  -k           Skip bad pages

EEPROM:
  -E <chip>    EEPROM type, e.g. 24c32, 93c46, 25q64
  -8           8-bit organization (Microwire)
  -f <bits>    Address size (Microwire)
  -s <bytes>   Page size (SPI EEPROM)

General:
  -P <prog>    Programmer: ch341a, ezp2019, auto (default)
  --debug      USB debug output
  --trace      Dump all SPI traffic
  -h           Help
```

## Examples

```bash
# Identify the chip
scriba -i

# Reliable backup — reads twice, compares, saves if identical
scriba -R backup.bin

# Safe flash — erases, writes, then verifies
scriba -W firmware.bin

# Single operations
scriba -r dump.bin -a 0 -l 0x400000    # read 4 MB from offset 0
scriba -w bootloader.bin -v            # write and verify
scriba -e                              # full chip erase

# Debugging
scriba --debug -i                      # see USB communication
scriba --trace -r dump.bin             # dump every SPI byte

# EEPROM
scriba -E 93c46 -r eeprom.bin
scriba -E 24c32 -w eeprom.bin
```

---

Scriba began as a streamlined fork of [SNANDer](https://github.com/McMCCRU/SNANDer)
by McMCC, later modified by Droid-MAX, and has since been heavily reworked by
[Paul Philippov](https://github.com/themactep) with EZP2019 support, code
optimizations, and structural improvements.

# Scriba — AGENTS.md

A direct reference for AI coding agents working on Scriba. No fluff. No hedging.

## What Is This

Scriba is a **flash programming tool for IP camera chips**. It talks SPI NOR, SPI NAND,
and EEPROM through cheap USB programmers (CH341A, CH347, EZP2019). Two targets:

- **CLI** — native C binary, `gcc` + `libusb-1.0`, runs on Linux.
- **Web** — same C code compiled to WASM via Emscripten, WebUSB bridge in JS,
  Bootstrap UI, Vite bundler. Runs in Chromium-based browsers.

Origin: forked from [SNANDer](https://github.com/McMCCRU/SNANDer) by McMCC,
modified by Droid-MAX, heavily reworked by Paul Philippov (`themactep`).
License: GPL-2.0-or-later.

---

## Architecture

### Layering (bottom → top)

```
┌──────────────────────────────────────────────────┐
│  main.c  (CLI)         web_main.c  (WASM API)    │  ← entry points
├──────────────────────────────────────────────────┤
│  flashcmd_api.c   —  chip-type dispatch           │  ← picks NOR/NAND/EEPROM
├──────────────────┬───────────────┬───────────────┤
│  spi_nor_flash   │ spi_nand_flash│ *eeprom.c      │  ← chip families
│                  │   + protocol  │   + microwire  │
│                  │   + tables    │                │
├──────────────────┴───────────────┴───────────────┤
│  spi_controller.c  —  programmer dispatch         │  ← CS, read/write bytes
├────────────────┬────────────────┬────────────────┤
│  ch341a_spi.c  │  ch347_spi.c   │  ezp2019_spi.c │  ← USB programmer drivers
│  libusb-1.0    │  libusb-1.0    │  libusb-1.0    │
└────────────────┴────────────────┴────────────────┘
```

### Entry Points

| File | Role |
|---|---|
| `src/main.c` | CLI: `getopt_long`, dispatch to `flashcmd_api`, `do_verify` helper |
| `web/src/web_main.c` | WASM: flat C API (`scriba_read_flash`, etc.), no CLI parsing |

Both use the same `struct flash_cmd` interface — `flash_read` / `flash_write` /
`flash_erase` function pointers set by `flash_cmd_init()`.

### `struct flash_cmd` (in `flashcmd_api.h`)

```c
struct flash_cmd {
    int (*flash_read)(unsigned char *buf, unsigned long from, unsigned long len);
    int (*flash_erase)(unsigned long offs, unsigned long len);
    int (*flash_write)(unsigned char *buf, unsigned long to, unsigned long len);
};
```

`flash_cmd_init()` probes in order:
1. SPI NOR (`snor_init`) — if it returns >0 bytes
2. SPI NAND (`snand_init`) — fallback
3. EEPROM variants (I2C, Microwire, SPI) — if `-E` was given on CLI

Whichever succeeds first fills in the `cmd` struct and the caller never knows
which flash type is underneath.

### Programmer Dispatch

Each driver exports one `const struct spi_programmer` (in `spi_controller.h`):
`init`, `shutdown`, `send_command`, `cs_select`, `cs_deselect`,
`libusb_version`, and an optional `reinit` (WASM recovery after a stall, and
re-applying clock/options).
`spi_controller.c` keeps a table of them; `spi_controller_init(type)` picks
one, and every `SPI_CONTROLLER_*` call goes through the active vtable. Types:
- `PROGRAMMER_CH341A = 0`
- `PROGRAMMER_EZP2019 = 1`
- `PROGRAMMER_AUTO = 2` (probes EZP2019, then CH347, then CH341A)
- `PROGRAMMER_CH347 = 3` (appended so the older values stay stable)

The flash layers call `SPI_CONTROLLER_*` functions, never the driver
functions directly. CS is separate from `send_command`: one flash command is
`cs_select`, one or more `send_command` calls, `cs_deselect`.

`spi_controller_set_speed_khz()` (CLI `--spi-speed`) records a requested SPI
clock before init; only drivers with a hardware clock (CH347) read it.
`spi_controller_set_options()` takes `SPI_OPT_*` flags;
`SPI_OPT_NO_CS_FLAGS` (CLI `--no-cs-flags`, web "Fast CS" unticked) makes
the CH347 driver use separate chip-select commands.

The CH347 driver holds writes back while CS is low and sends them as one
frame (CS high, a following read, or a full frame flushes them), and folds a
short read after held writes into one full-duplex `0xC2` frame. So a call
returning success does not mean the bytes are on the wire yet; a USB error
sets a sticky failure that fails every later call until `reinit`. On CH347T
firmware 0x0341+ or a CH347F (version from the `0xCA` chip-info read) it
also defers `cs_select()` and carries CS in `0xC2` length flags (bit 7 =
assert before, bit 6 = release after), as WCH's Linux driver does in its
default mode. Nothing is held past `cs_deselect()`.

Sending several commands in one USB transfer does NOT work: CH347T firmware
0544 answers with an error and drops what follows (tested 2026-11). WCH's
"quick speed" mode merges SPI transfers into one `0xC2` command instead.

### USB Drivers

- **ch341a_spi.c** (26 KB, most complex): bit-bangs SPI over CH341A USB-I2C/SPI
  bridge IC. Also has I2C (`ch341a_i2c.c`), GPIO (`ch341a_gpio.c`), and
  Microwire (`bitbang_microwire.c`) support for EEPROM variants. Its USB
  handle is a global named `handle` that those helpers use directly.
- **ch347_spi.c**: WCH CH347T (mode 1, `1a86:55db`, interface 2) and CH347F
  (`1a86:55de`, interface 4). Hardware SPI master with framed bulk commands on
  EP 0x06/0x86: `0xC0` config, `0xC1` CS, `0xC4` write (4-byte status reply),
  `0xC3` read (`[C3 len data]` frames). Read frames are 510 bytes on older
  firmware and up to 4 KB on newer firmware, so the driver parses IN data as a
  stream of frames and always requests whole 512-byte packets (no overflow) that
  the device is certain to send (no stall). Interface 0 is the UART; only
  the SPI interface is claimed. I2C/Microwire EEPROM is CH341A-only for now
  (`main.c` refuses `-E` for those on a CH347).
- **ezp2019_spi.c** (22 KB): packet protocol over bulk USB endpoints for the
  EZP2019/2023 programmer family. Simpler than CH341A — dedicated SPI commands.

### Global State

Several globals cross-cut the codebase. All defined in `main.c` and declared
`extern` where needed:

| Variable | File | Meaning |
|---|---|---|
| `active`, `active_type` | `spi_controller.c` | Active programmer vtable and its type |
| `debug_enabled`, `trace_enabled` | `main.c` | USB/SPI debug logging |
| `bsize` | `spi_nor_flash.c` | Flash block erase size (set at init) |
| `ECC_fcheck`, `ECC_ignore`, `OOB_size`, `Skip_BAD_page`, `_ondie_ecc_flag` | NAND subsystem | ECC control flags |
| `eepromsize`, `mw_eepromsize`, `seepromsize`, `spage_size`, `org`, `fix_addr_len` | EEPROM subsystem | EEPROM parameters |
| `spi_chip_info` | NOR subsystem | Pointer to detected chip's table entry |

No global mutex. Single-threaded. No reentrancy concerns for the CLI, but the
WASM build uses ASYNCIFY (Emscripten) — libusb calls are async-suspended.

---

## Source File Map

```
src/
├── main.c                     CLI entry, option parsing, operation dispatch
├── flashcmd_api.c / .h        Flash-type detection, cmd struct init
├── flash_layout.c / .h        Partition layout of an image: mtdparts + header scan
├── spi_controller.c / .h      Programmer abstraction, CS, byte r/w
├── ch341a_spi.c / .h          CH341A USB-SPI driver
├── ch341a_i2c.c / .h          CH341A I2C bit-bang (EEPROM)
├── ch341a_gpio.c / .h         CH341A GPIO helpers
├── ch347_spi.c / .h           CH347T/CH347F USB-SPI driver
├── ezp2019_spi.c / .h         EZP2019/2023 USB-SPI driver
├── spi_nor_flash.c / .h       SPI NOR commands, chip table, erase/write
├── spi_nand_flash.c / .h      SPI NAND core
├── spi_nand_flash_protocol.c  NAND protocol layer
├── spi_nand_flash_tables.c    NAND chip ID table (73 KB — largest file)
├── spi_nand_flash_defs.h      NAND command/register definitions
├── spi_nand_flash_feature.h   NAND feature register helpers
├── timer.c / .h               Progress display, elapsed time
├── types.h                    u8/u16/u32/u64 typedefs
├── i2c_eeprom.c               I2C EEPROM support
├── spi_eeprom.c / .h          SPI EEPROM support
├── mw_eeprom.c                Microwire EEPROM support
├── bitbang_microwire.c / .h   Microwire 3-wire bit-bang
├── 40-persistent-ch341a.rules udev rules
├── 40-persistent-ch347.rules
└── 40-persistent-ezp2019.rules

web/
├── index.html                 Single-page UI (Bootstrap dark theme)
├── src/
│   ├── main.js                Vite entry, module loader
│   ├── app.js                 UI logic, WASM binding, operations
│   ├── web_main.c             C entry for WASM (flat API)
│   └── libusb-webusb.js       JS reimplementation of libusb API over WebUSB
├── libusb-stub/libusb-1.0/    Stub libusb headers for WASM build
├── CMakeLists.txt             Emscripten build
├── build.sh                   One-shot build script
├── package.json               Vite + Bootstrap
└── vite.config.js             Outputs to web/dist/
```

---

## Build

### CLI

```bash
make                  # dynamic linking (libusb shared)
make static           # downloads + builds libusb from source, static link
sudo make install     # binary → /usr/bin, udev rules → /etc/udev/rules.d
```

CFLAGS: `-std=gnu99 -Wall -O2 -D_FILE_OFFSET_BITS=64`. No `-Wextra` in the
default Makefile (PHASE1_SUMMARY claims it passes, but the Makefile doesn't
set it — if you're adding warnings, add them to the Makefile, not just the
CLI invocation).

`make static` downloads libusb 1.0.30 tarball into `dl/`, builds it into
`build/usr/`, and links statically. The `CONFIG_STATIC` define gates the
difference in `title()` output and libusb include path.

### Web (WASM)

```bash
cd web
./build.sh             # emcmake + npm install + vite build, output to dist/
# or for dev:
npm run dev            # Vite dev server on localhost:5173
```

The WASM build uses the same `.c` files as CLI except `main.c` → `web_main.c`.
The `libusb-webusb.js` is a JS shim that implements libusb API on top of
WebUSB (browsers can't do raw USB). `ASYNCIFY=1` is on because WebUSB calls
are inherently async.

Exported WASM functions: `scriba_init`, `scriba_detect_chip`, `scriba_read_flash`,
`scriba_write_flash`, `scriba_erase_flash`, `scriba_shutdown`, `scriba_get_version`,
etc. See CMakeLists.txt `EXPORTED_FUNCTIONS` for the full list.

---

## Design Patterns

### Function-pointer dispatch (flash_cmd)
The `flash_cmd` struct is the only vtable-like pattern. It's filled once at
init based on chip probe, then the CLI/WASM layer calls through it. Clean.
Don't break this abstraction by casting to concrete types in main/web_main.

### Programmer vtable table
`spi_controller.c` holds a `programmers[]` table of `{type, vtable}` pairs in
auto-probe order; `spi_controller_init()` walks it. Adding a programmer is
one table row plus the driver file — see "Adding a new programmer" below.

### malloc + free pairing
The PHASE1 work added proper `malloc` return checks. Every `malloc` in `main.c`
has a corresponding `free` you can see within 20 lines. The convention:
- `free(buf); goto out;` on error
- `goto okout;` on success
- `out:` label does programmer shutdown + `return 1`
- `okout:` label does programmer shutdown + `return 0`

The flash layers (`spi_nor_flash.c`, `spi_nand_flash.c`) do their own allocation
with less rigorous checking. Audit those before claiming "no leaks."

### Chip tables
- NOR: static array in `spi_nor_flash.c`, each entry has JEDEC ID, name, size,
  erase block size, supported opcodes.
- NAND: massive array in `spi_nand_flash_tables.c` (73 KB), each entry has
  manufacturer ID, page size, OOB size, ECC info.

Detection is linear scan through the table matching read ID bytes. qsort was
proposed in OPTIMIZATION_PLAN.md but not implemented — the tables are small
enough that linear scan is fine.

---

## Common Tasks

### Adding a new flash chip
1. Add entry to the appropriate table:
   - NOR: `spi_nor_flash.c` — find the right `struct SPI_NOR_FLASH_INFO_T` array
   - NAND: `spi_nand_flash_tables.c` — find the right `SPI_NAND_FLASH_INFO_TABLE` array
2. Match by JEDEC ID or NAND manufacturer+device ID.
3. Verify `-i` detects it, `-R` reads it correctly, `-W` writes + verifies.
4. Test on actual hardware. Datasheets are the only truth — not what some
   other chip in the same family does.

### Adding a new programmer
`ch347_spi.c` is the most recent worked example. Steps:
1. Write `src/<name>_spi.c` exporting a `const struct spi_programmer`
   (keep the USB handle `static`; `handle` is already taken by the CH341A).
2. Append a `PROGRAMMER_*` value in `spi_controller.h` and a row in
   `programmers[]` in `spi_controller.c`.
3. `main.c`: `-P` name, not-found message, usage text.
4. Add the source to `SRCS` in both `Makefile` and `web/Makefile`.
5. Add a udev rules file and install it from `install-rules`.
6. Web: add the VID:PID to the `requestDevice` filters in `web/src/app.js`
   and to `SCRIBA_PIDS` in `web/src/libusb-webusb.js`, and the name to the
   `progNames` map in `app.js`.
7. Wire up in `flashcmd_api.c` if needed (usually not — flashcmd only
   cares about the chip, not the programmer).

### Changing CLI behavior
Everything is in `main.c`. The `getopt_long` block handles option parsing.
The `op` variable controls which operation runs. Adding a new operation
means:
1. New case in `getopt_long`
2. New `if (op == 'X')` block before the `r || w` block
3. Respect the `out:` / `okout:` label pattern for cleanup

### Changing WebUI behavior
- UI: `web/index.html` (Bootstrap 5, dark theme) + `web/src/app.js`
- WASM bridge: `web/src/web_main.c` for C side, `web/src/libusb-webusb.js`
  for USB shim
- The JS app calls `Module.ccall` or `Module._scriba_*` to invoke WASM

---

## Constraints & Gotchas

### No `-Wextra` in Makefile
Despite PHASE1_SUMMARY claiming "compiles with -Wall -Wextra without warnings,"
the Makefile only sets `-Wall`. If you add `-Wextra`, expect a burst of warnings
from the legacy code. Fix them if you touch the file, but don't add `-Wextra`
to the default build without a plan.

### Global state is pervasive
Adding threading or reentrancy would require gutting `programmer_type`,
`debug_enabled`, `bsize`, and the NAND global flags. Don't.

### WEB: ASYNCIFY means synchronous-looking C code
The WebUSB shim (`libusb-webusb.js`) returns Promises, but Emscripten's
ASYNCIFY makes the C code wait synchronously. If you change the shim, you
must keep `ASYNCIFY_IMPORTS` in CMakeLists.txt in sync with all async
functions. Missing one = runtime hang.

### WEB: libusb-stub headers
The WASM build uses stub headers in `web/libusb-stub/libusb-1.0/` — these
declare libusb functions but the actual implementation is in
`web/src/libusb-webusb.js`. The stubs must match the JS implementation's
signatures. libusb version skew here will cause silent failures.

### EEPROM support is compile-time conditional
`#ifdef EEPROM_SUPPORT` guards EEPROM files. `Makefile` sets
`EEPROM_SUPPORT = yes` and adds the source files + `-DEEPROM_SUPPORT`.
The web `CMakeLists.txt` does NOT include EEPROM source files — WASM build
is SPI NOR/NAND only.

### `memcmp` is used for verification now
PHASE1 replaced byte-by-byte file comparison with `memcmp()`. The standalone
`flashcmd_verify()` function in `flashcmd_api.c` also uses `memcmp`, but
`do_verify()` in `main.c` reimplements the same pattern. Consolidating these
was on the PHASE2 plan but didn't happen. If you touch verification, use
`do_verify()` as the canonical pattern — it allocates, checks, frees, and
handles the NAND case correctly.

### udev rules are installed by `make install`
If a programmer isn't detected, check `dmesg` for USB permissions. The udev
rules grant `0664` access. Replug the device after install.

### Test scripts reference `/tmp/opencode/scriba`
The test scripts (`test_suite.sh`, `test_advanced.sh`) and docs reference
paths from an earlier checkout. The scripts assume `./scriba` is in CWD.
Review before running.

### CH341A needs 3.3V modification
The black-PCB CH341A ships with 5V on the data lines. Flash chips expect 3.3V.
Users must physically modify the board. The tool can't detect this — it'll
just silently fry chips. The README links to the modification guide.

---

## Optimization History

Phase 1 (2026-05-21, complete):
- `malloc` error checking on all allocation sites in `main.c`
- `memcmp` instead of byte-by-byte file verification
- Consistent `free` on error paths

Phases 2–4 (planned, not executed):
- Consolidate verify logic (remove duplication between `do_verify` and `flashcmd_verify`)
- ECC check table refactoring with lookup tables
- Progress display consolidation into `timer.c`
- Buffer reuse optimization
- USB transfer buffer management

---

## Style & Voice

When working on Scriba, adopt the `thingino-dev-persona`:
- Datasheet citations for magic numbers. `sleep(100)` is a crime;
  `sleep(100) /* DS §7.3.2: 100ms min reset recovery */` is engineering.
- Every `malloc` must have a visible `free` path.
- No Unicode in `.c`, `.h`, `.mk`, or `.sh` files. ASCII only.
  Use `---` not `—`, `->` not `→`.
- The hardware is the only source of truth. If the datasheet and the
  comment disagree, the datasheet wins.
- Minimal changes. Don't refactor unless it saves bytes, cycles, or
  complexity. Fashion is for webdevs.
- `shellcheck` on all `.sh` files. `set -euo pipefail` at the top.

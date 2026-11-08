# AGENTS.md — Persistent Context for AI Agents

## Project
Thingino **scriba** — a SPI flash programmer tool for CH341A, CH347 (CH347T in
mode 1, or CH347F), EZP2019/EZP2019+ and EZP2023 programmers. It runs as a
native CLI and as a WebUSB/WASM page.

## Layout
- `src/core/` — shared library: lifecycle (`scriba.c`), flash dispatch
  (`flashcmd_api.c`), operations and validation; `flash_layout.c` identifies
  the partitions of an image (mtdparts + header scan) for the CLI and web.
- `src/hal/` — hardware abstraction: `spi_controller.c` backend dispatch,
  `usb/` transport HAL (libusb / WebUSB), and one directory per programmer
  (`ch341a/`, `ch347/`, `ezp2019/`).
- `src/protocol/` — `nor/`, `nand/`, `eeprom/` flash protocols.
- `src/ui/cli/` — CLI frontend; `web/` — Vite/WebUSB frontend.
- `res/` — udev rules.

## Invariants worth knowing
- One backend row per programmer in `src/hal/spi_controller.c`. The USB HAL is
  the only place that talks libusb or WebUSB.
- The CH341A asserts CS inside every `send_command()` (`pluck_cs()` /
  `cs_deassert()`), so a command must not be split across calls. NOR reads
  therefore chunk at 4096 bytes in the protocol layer with a fresh READ each
  time (`SNOR_READ_CHUNK` is larger natively). The CH347 holds CS across calls
  through the backend's `chip_select_low/high`.
- `snor_write()` skips pages that are all 0xFF (programming can only clear
  bits), and unprotects the chip once per call.
- WebUSB cannot cancel a posted `transferIn`. `usb_bulk_stream()` sets a
  `dead` flag on the first failure or timeout so late completions neither
  write the C buffer nor queue more transfers.
- Auto-detect order is EZP2019 → CH347 → CH341A. With more than one supported
  programmer connected the CLI shows a menu; `-P` selects one directly.
- The CH347 is SPI flash and SPI EEPROM only; I2C and Microwire EEPROMs use
  CH341A-specific code today.

## Historical fixes (still relevant)
- **CH341A read corruption**: CS was not held across the UIO → SPI stream
  transition. `pluck_cs()` embeds the assertion in the data transfer and
  `cs_deassert()` releases it afterwards; all chip-select calls in the
  protocol layer are no-ops for the CH341A.
- **Writes silently dropped**: ZB25VQ128 had SR3 bit 2 set. `snor_unprotect()`
  now clears SR3 for the ZB25 family; status polling handles SR_EPE and
  retries. `full_erase_chip()` checks WEL before BE in the WASM build.
- **WASM status polling**: short waits poll back to back to the native
  wall-clock budget; erase keeps 100 ms polling.
- **Read twice** (CLI `-R`, web checkbox) compares a second pass 64 KB at a
  time and saves nothing on a mismatch. This is the first thing to reach for
  when a clip or supply is marginal.

## Build and test
```bash
make                      # native CLI -> build/bin/scriba
make web                  # WASM -> web/public/wasm/scriba.{js,wasm}
./test.sh                 # CLI regression suite, no hardware needed
SCRIBA_HW=1 SCRIBA_HW_PROG=ch341a ./test.sh   # + identify and a 4 KB read
```

## Useful commands
```bash
scriba -i                                   # identify
scriba -R /tmp/read.bin                     # read twice, save if identical
scriba -r /tmp/sector.bin -a 0 -l 65536     # read 64 KB
scriba -a 0 -l 65536 -w /tmp/pattern.bin -v # write and verify
scriba --layout dump.bin                    # partition layout, no programmer
```

## Hardware notes
- CH347T boards must be in mode 1 (UART + SPI + I2C) to expose SPI
  (`1a86:55db`); CH347F is `1a86:55de`. Firmware 0341+ supports chip-select
  flags in data frames; `--no-cs-flags` disables them.
- Check the flash supply voltage. Winbond `...FW`/`...JW` parts are 1.8 V and
  need level shifting on a 3.3 V CH341A; sporadic bit flips in repeated reads
  usually mean a marginal clip or a voltage mismatch. `-R` is designed to
  catch exactly that.

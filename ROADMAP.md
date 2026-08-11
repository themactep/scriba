# Scriba — Roadmap & Critical Review

Last updated: 2026-07-29. Checked boxes mean done. Don't check them prematurely.

---

## Critical — Bugs

These can cause crashes, data corruption, or silent misbehavior.

- [x] **`int long long len` in `main.c:138`** — This is not valid C. Two type specifiers
  (`int` + `long long`) on the same declaration. GCC happens to parse it as
  `long long`, but clang rejects it. Fix: remove the stray `int`.
- [x] **`flashcmd_verify` hardcodes `snand_read`** — `flashcmd_api.c:72` calls
  `snand_read()` directly, bypassing the `flash_cmd` dispatch. If called on a
  NOR flash chip, it reads via NAND protocol → garbage or hang. Currently
  dead code (CLI uses its own `do_verify`), but it's exported in the header.
  Fix: removed the function entirely — dead code, never called.
- [x] **`test_write_fail_flag` is dead code** — `spi_nand_flash.c:629`. Declared
  as `int`, set to 0, never read. Removed.
- [x] **Test scripts are fake** — `test_suite.sh` and `test_advanced.sh` don't
  actually test anything. They print PASS unconditionally regardless of
  real behavior. `BIN_DIR` hardcodes `/tmp/opencode/scriba` which doesn't
  exist on any development machine. These are documentation placeholders,
  not tests. Deleted. TESTING.md retains the manual test procedures.

---

## High — Code Quality & Safety

Issues that won't crash today but degrade reliability and maintainability.

- [x] **NOR flash progress printing bypasses `timer.c`** — `snor_erase` and
  `snor_read` use inline `printf("\bErase %ld%%...")` with hardcoded
  backspace-counting tricks. `spi_nand_flash.c` uses `timer_print_progress()`.
  Two completely different progress display mechanisms. Fixed: unified timer API
  (`timer_progress(msg, current, total)`), all call sites (NOR, NAND, EEPROM)
  now use the same function.
- [x] **`timer.c` progress is fragile** — `timer_print_progress` uses `\b`
  backspace tricks to overwrite, `progress_printed` flag with unclear reset
  logic, `volatile` on a variable that can't be accessed concurrently. Fixed:
  rewritten to use `\r` + `\e[K` (ANSI clear-to-EOL), merged `timer_progress`
  and `timer_print_progress` into one function, removed `volatile`.
- [ ] **17 `#ifdef __EMSCRIPTEN__` blocks in `spi_nor_flash.c`** —
  Platform-specific chunked reads (4096-byte blocks for WASM), alternate
  SPI command dispatch (`ch341a_spi_send_command` vs `SPI_CONTROLLER_*`),
  and suppressed `printf` calls litter the NOR driver. The WASM build
  concerns should be isolated in the `spi_controller` layer or in
  `web_main.c`, not splattered through the flash driver. Every new chip
  quirk now needs to consider two code paths.
- [x] **`spi_nand_flash_core.c` (447 lines) not compiled** — Deleted. Globals
  already defined in `spi_nand_flash.c`. Dead code from a refactor.
- [x] **Duplicate chip entries in `chips_data`** — `EN25XQ128A` (lines
  585-586), `PY25Q64HA` (709-710). Removed exact duplicates. The `IS25LP256D`
  entries have different densities (256Mb vs 512Mb) — added clarifying comments.
- [x] **Type inconsistency: `uint8_t` vs `u8`** — Converted three functions in
  `spi_nor_flash.c` (`snor_wait_ready`, `snor_read_rg`, `snor_write_rg`) to `u8`.
- [x] **`bmt_oob_size` type mismatch** — `OOB_size` changed from `int` to
  `u32`. All comparisons now same signedness. Removed unnecessary casts.
- [x] **Commented-out `snor_dbg` calls** — Removed.
- [x] **`/* Stray code removed: ... */` in `spi_nor_flash.c`** — Removed.

---

## Medium — Architecture & Maintainability

Structural issues that make the codebase harder to reason about.

- [x] **`do_verify` (main.c) and `flashcmd_verify` (flashcmd_api.c) are
  near-duplicates** — Consolidated: new `flashcmd_verify(struct flash_cmd*, ...)`
  in flashcmd_api.c, called from both `-W` and `-w -v` paths in main.c. Old
  broken `flashcmd_verify` (snand_read hardcode) removed.
- [x] **`timer_progress()` / `timer_print_progress()` split** — Two
  functions where one would do. Merged into `timer_progress(msg, current, total)`
  returning void with rate-limiting internal.
- [x] **`_SPI_NAND_SEMAPHORE_LOCK/UNLOCK` macros** — Removed. No-ops in
  a single-threaded userspace program. All 14 call sites deleted.
- [x] **CH341A SPI driver is 899 lines with its own USB transfer batching** —
  Introduced `struct spi_programmer` vtable in `spi_controller`. Each driver
  provides a vtable instance. `spi_controller_init()` handles auto-detect;
  all `SPI_CONTROLLER_*` functions dispatch through the vtable — no more
  if/else on `programmer_type`. Adding a third programmer is now trivial.
- [x] **`spi_nand_flash_tables.c` is 73 KB / 1839 lines** — Documented
  field layout in header comment. Pure data, splitting by manufacturer adds
  no value.

---

## Low — Cleanup & Polish

Not urgent. Fix opportunistically when touching nearby code.

- [x] **TODO comments in `ch341a_i2c.h`** — Removed. Magic firmware blobs
  are correct as raw byte strings; structs would add alignment risk.
- [x] **`volatile int progress_printed` in `timer.c`** — No thread, no ISR,
  no DMA. Removed (entire timer.c rewritten).
- [x] **Missing JEDEC IDs in NOR chip table** — Replaced REVISIT with
  documentation of the upper-16-bit matching convention. Full IDs require
  per-chip datasheets; partial IDs work correctly via the stripped match.
- [x] **`snor_progress` state tracking** — Kept. Debug-only, zero cost
  when disabled, useful for diagnosing erase/program failures.
- [x] **Add `-Wextra` to Makefile** — Added. Code compiles cleanly with
  `-Wall -Wextra` (zero warnings).
- [ ] **`#ifdef __EMSCRIPTEN__` uses `ch341a_spi_send_command` directly** —
  In SPI_CONTROLLER functions, the WASM path sometimes bypasses
  `spi_controller.c` and calls `ch341a_spi_send_command` directly (e.g.,
  `snor_read_sr`, `snor_read_devid`). This means WASM doesn't support
  EZP2019 even though the web UI doesn't expose it. If EZP2019 support is
  added to the web build, these direct calls become bugs.
- [x] **`flashcmd_verify` takes `file_len` parameter it doesn't use** —
  Old function deleted in Sprint 1. Replacement has no unused parameters.
- [x] **`nandflash_init`, `nandflash_read`, `nandflash_erase`,
  `nandflash_write` are wrapper functions** — Inlined into `snand_*`
  functions. Removed 4 unnecessary call frames.

---

## Web/WASM Specific

- [ ] **EEPROM support missing from WASM build** — `CMakeLists.txt` doesn't
  include EEPROM source files, and `web_main.c` doesn't expose EEPROM
  functions. If the web UI shows EEPROM options, they won't work. Decide:
  add EEPROM to WASM or remove the UI elements.
- [ ] **`ASYNCIFY_IMPORTS` must stay in sync** — If any new libusb function
  is added to the `libusb-webusb.js` shim, it must also be added to the
  JSON file in `CMakeLists.txt` or the WASM build hangs at runtime. Add a
  comment in `libusb-webusb.js` and `web_main.c` documenting this constraint.
- [ ] **Web build needs 64 MB initial memory** — `INITIAL_MEMORY=67108864`
  in CMakeLists.txt. That's the entire flash read buffer being allocated
  in WASM heap. If flash sizes grow (128MB NAND chips), this breaks.
  `ALLOW_MEMORY_GROWTH=1` is set, so it won't crash, but growth is slow
  and may trigger browser OOM on 32-bit. Document the practical limit.

---

## Roadmap — Suggested Order of Work

### Sprint 1: Fix the bugs (Week 1)
1. Fix `int long long` in main.c
2. Delete or fix `flashcmd_verify` to use `flash_cmd` dispatch
3. Delete `test_write_fail_flag`
4. Delete or rewrite test scripts (remove fake tests)
5. Remove duplicate chip entries (verify IS25LP256D densities)

### Sprint 2: Unify the progress/timer layer (Week 1-2)
1. Merge `timer_progress` + `timer_print_progress` into one function
2. Route NOR progress through the unified timer (remove inline printf tricks)
3. Remove `volatile` from timer globals
4. Fix the `\b`-counting to use `\r` + clear-to-EOL

### Sprint 3: Consolidate verification (Week 2)
1. Move `do_verify` logic into `flashcmd_api.c`, using `struct flash_cmd*`
2. Use the same function from CLI and WASM
3. Delete the broken `flashcmd_verify`

### Sprint 4: Clean up #ifdef WASM (Week 2-3)
1. Move WASM chunking logic into `spi_controller.c` (make `Read_NByte` handle
   large reads internally regardless of platform)
2. Remove direct `ch341a_spi_send_command` calls from NOR driver
3. Verify EZP2019 would work in WASM build after cleanup

### Sprint 5: Type hygiene & cleanup (Week 3)
1. Convert `uint8_t` → `u8` in NOR driver
2. Fix `bmt_oob_size` / `OOB_size` signedness
3. Remove dead commented-out code
4. Add `-Wextra` to Makefile, fix warnings

### Sprint 6: Architecture hardening (Ongoing)
1. Delete or integrate `spi_nand_flash_core.c`
2. Remove `_SPI_NAND_SEMAPHORE_*` no-ops
3. Split `spi_nand_flash_tables.c` by manufacturer
4. Extract common USB patterns from CH341A/EZP2019 drivers

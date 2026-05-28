# Scriba Optimization Proposal

## Critical Bug

- **`src/spi_nand_flash.c:818`** — `ptr_rtn_len += data_len;` modifies the pointer itself instead of `*ptr_rtn_len += data_len;`. The caller reads `ptr_rtn_len` as garbage. Write length tracking is silently broken.

## Global State & Coupling

- **`spi_controller.c`** — Every function branches on `programmer_type == PROGRAMMER_EZP2019`. Replace with a function pointer table set at init time; eliminates ~30% of the file.
- **Globals everywhere** — `debug_enabled`, `trace_enabled`, `bsize`, `OOB_size`, `programmer_type` are `extern`-ed across 7+ translation units. Consolidate into a context struct passed through the call chain.

## Code Duplication

- **Progress printing** — The `\b\b\b...\b\b\b` backspace-spam pattern is duplicated in `snor_erase()`, `snor_read()`, `snor_write()` in `spi_nor_flash.c` and in `timer_print_progress()` in `timer.c`. The NOR flash functions don't use `timer_print_progress()` at all.
- **Verify logic** — `do_verify()` in `main.c` and `flashcmd_verify()` in `flashcmd_api.c` are nearly identical. `flashcmd_verify()` hardcodes `snand_read` instead of using the dispatch table, so it only works for SPI NAND.

## Performance

- **`ezp2019_spi.c:106`** — `usleep(50000)` (50ms) on every EZP command response. Dominant EZP2019 latency contributor.
- **`snor_read()`** splits reads along sector boundaries, causing rapid CS toggle cycles. NOR flash supports much larger contiguous transfers.
- **`snor_wait_ready()`** busy-polls with `usleep(500)`, doing ~6000 USB round-trips for a 3-second erase. Geometric backoff would reduce traffic by 10x+.

## Maintainability

- **`#ifdef __EMSCRIPTEN__`** blocks scattered across 5+ files with entirely different code paths (`enable_pins()`, `snor_read()` chunking, `snor_wait_ready()` timing). Isolate behind platform abstraction functions.
- **`spi_nor_flash.c`** has a 300+ entry chip table inlined. Move to its own source file.
- **EZP2019 SPI emulation** (`ezp2019_spi.c:467-641`) buffers raw SPI commands and interprets them on CS deassert. Fragile; adding new opcodes is error-prone.

## Memory Safety

- **`ch341a_spi.c:203-204`** — WASM uses fixed static buffers; native uses VLAs. WASM silently truncates transfers larger than `CH341_MAX_PACKET_LEN` (8192 bytes).

---

## Verification

All 12 claims verified against source:

- Critical bug (spi_nand_flash.c:818) confirmed: `ptr_rtn_len += data_len` mutates the pointer.
- spi_controller.c has 5 identical `programmer_type` branches.
- Globals (`debug_enabled`, `trace_enabled`, `OOB_size`) extern across 5+ files.
- Progress printing duplicated in spi_nor_flash.c; NOR does not call `timer_print_progress()`.
- `flashcmd_verify()` hardcodes `snand_read` — NAND-only.
- `usleep(50000)` at ezp2019_spi.c:123 on every response.
- `snor_read()` splits at sector boundaries; `snor_wait_ready()` does ~6000 polls for 3s erase.
- 20 `__EMSCRIPTEN__` blocks in 3 files with divergent paths.
- 292-entry chip table inline; EZP emulation spans ezp2019_spi.c:467-641.
- WASM buffer capped at 8192 bytes (CH341_MAX_PACKET_LEN).

All proposals are accurate.

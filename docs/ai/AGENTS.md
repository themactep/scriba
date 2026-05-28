# AGENTS.md — Persistent Context for AI Agents

## Project
Thingino **scriba** — a CH341A SPI flash programmer tool.

## Status: All Issues Fixed ✓

**Read/Write/Erase all work 100%** after two fixes:

### Fix 1: Read corruption (82.7% error rate → 0 errors)
- **Root cause**: CH341A CS output state not properly being held during SPI transactions — a glitch between UIO commands and SPI stream commands caused incorrect data shifting
- **Fix**: Embed CS assertion (`pluck_cs()`) at the start of every USB bulk transfer in `ch341a_spi.c`, and deassert CS (`cs_deassert()`) after each transfer. Make CS lifecycle internal to `ch341a_spi_send_command()`
- Removed all explicit `Chip_Select_Low/High()` calls from `spi_nor_flash.c` — they are now no-ops
- Added `SPI_CONTROLLER_WriteRead_NByte()` backend function for combined write+read transactions
- Set SPI speed to 20KHz (was 750KHz) for reliability
- Added `SNOR_READ_CHUNK=65536` to prevent VLA stack overflow on large reads

### Fix 2: Write/Erase silently dropped (WEL cleared immediately)
- **Root cause**: ZB25VQ128 had SR3 register set to 0x74, with **bit 2 (0x04)** blocking write/erase operations. The chip accepted WREN (WEL=1) but then silently dropped SE/PP/BE commands, clearing WEL to 0 immediately
- The existing code only read/cleared SR3 for chips with names starting with "XM" or "NM" — ZB25VQ128 was not matched
- **Fix**: Modified `snor_requires_sr3()` to also match "ZB25" prefix. Added `SR3_TB (0x04)` define and clearing logic. On identify, scriba now reads SR3, sees bit 2 is set, and clears it, removing the write protection
- Flashrom also failed to erase before this fix, confirming it's a chip-level issue not specific to scriba

### Verification
- Full 16MB read-compare: **Compare Status: OK — Both reads are identical**
- Sector erase (0xD8): **100% erased to 0xFF**
- Page program (0x02): **256/256 bytes match**
- Full 8MB firmware write + verify: **0 errors**
- All operations verified at 20KHz SPI speed on CH341A rev 3.0.4 + ZB25VQ128

## Key Files
- `src/ch341a_spi.c` — `pluck_cs()`, `cs_deassert()`, modified `ch341a_spi_send_command()`
- `src/spi_controller.c` — `SPI_CONTROLLER_WriteRead_NByte()`, no-op chip_select for CH341A
- `src/spi_nor_flash.c` — `snor_requires_sr3()` includes "ZB25", `SR3_TB` clearing in `snor_unprotect()`
- `src/spi_nor_flash.h` — `SR3_TB 0x04` define

## Test Commands
```bash
# Rebuild
make -C /path/to/scriba

# Identify
scriba -i

# Read (full chip, compare two passes)
scriba -R /tmp/read.bin

# Write + verify
scriba -a 0 -l 65536 -w /tmp/pattern.bin

# Read 64KB
scriba -a 0 -l 65536 -r /tmp/sector.bin
```

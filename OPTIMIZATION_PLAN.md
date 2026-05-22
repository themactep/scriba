# Scriba Code Optimization Plan

## Overview

This document outlines the optimization roadmap for the Scriba codebase. The goal is to improve code quality, reduce duplication, enhance maintainability, and ensure robustness without changing functionality.

## Optimization Categories

### 1. Memory Management & Safety
- [ ] Add error checking for all `malloc()` calls
- [ ] Implement consistent buffer size validation before allocation
- [ ] Add null pointer checks for all allocated memory
- [ ] Add `free()` calls on all error paths
- [ ] Use stack allocation or reuse buffers where possible

### 2. Code Deduplication & Refactoring
- [ ] Create reusable verification function for file comparison
- [ ] Consolidate progress printing logic into `timer.c`
- [ ] Refactor `ecc_fail_check()` in `spi_nand_flash.c` to use a lookup table for manufacturer-specific logic
- [ ] Extract common buffer allocation patterns into helper functions
- [ ] Remove duplicated verify logic in `main.c`

### 3. Error Handling Improvements
- [ ] Create unified error handling macros
- [ ] Add validation of file operations before allocation
- [ ] Implement consistent error code propagation
- [ ] Add proper cleanup on all error paths
- [ ] Fix missing null pointer checks

### 4. Performance Optimizations
- [ ] Replace byte-by-byte file comparison with `memcmp()`
- [ ] Optimize USB transfer buffer management in `ch341a_spi.c`
- [ ] Consider using `qsort()` for chip detection instead of linear search
- [ ] Reduce redundant file operations

### 5. Code Quality & Maintainability
- [ ] Add static analysis warnings (gcc -Wextra -Wall)
- [ ] Implement `const` correctness for string parameters
- [ ] Fix variable shadowing issues
- [ ] Add proper type definitions from `types.h`
- [ ] Document all external symbols in header files

---

## Test Suite

### Test 1: Buffer Allocation Error Handling
**File**: `main.c`
**Objective**: Ensure all `malloc()` calls check return value
**Steps**:
1. Allocate memory with insufficient size
2. Verify program returns error code 1
3. Check no segfault occurs

**Expected**: Error message to stderr, exit code 1

### Test 2: File Comparison Optimization
**File**: `main.c:384-405`, `main.c:592-609`
**Objective**: Verify optimized file comparison works correctly
**Steps**:
1. Write binary file with known content
2. Read file into buffer
3. Compare using optimized `memcmp()` instead of byte-by-byte
4. Verify identical files match
5. Verify modified files detect mismatch

**Expected**: Faster comparison, correct mismatch detection

### Test 3: ECC Check Table Refactoring
**File**: `spi_nand_flash.c:91-361`
**Objective**: Verify ECC check logic works with lookup table
**Steps**:
1. Test GigaDevice chips with various ECC status register values
2. Test other manufacturers (MXIC, Winbond, etc.)
3. Verify bad block detection works for all chip types
4. Verify no false positives

**Expected**: Same behavior, cleaner code structure

### Test 4: Progress Printing Consolidation
**File**: `timer.c`
**Objective**: Ensure progress display works correctly
**Steps**:
1. Erase large chip (1MB+)
2. Read large chip (1MB+)
3. Write large chip (1MB+)
4. Verify percentage display updates correctly

**Expected**: 0-100% display, no corruption

### Test 5: Buffer Reuse Optimization
**File**: `main.c:329-334, 422-430`
**Objective**: Verify buffers are reused when possible
**Steps**:
1. Test `-W` mode (erase+write+verify)
2. Test `-R` mode (read twice)
3. Verify no memory leaks
4. Verify no stack corruption

**Expected**: Same functionality, reduced allocations

---

## Implementation Checklist

### Phase 1: Critical Safety (Week 1)
- [ ] Test 1: Buffer allocation error handling
- [ ] Add null pointer checks for all allocated memory
- [ ] Implement consistent `free()` on error paths
- [ ] Add validation of file sizes before allocation

### Phase 2: Code Quality (Week 2)
- [ ] Test 2: File comparison optimization
- [ ] Create reusable verification function
- [ ] Implement `const` correctness
- [ ] Add static analysis warnings

### Phase 3: Refactoring (Week 3)
- [ ] Test 3: ECC check table refactoring
- [ ] Consolidate progress printing
- [ ] Remove duplicated verify logic
- [ ] Extract common patterns

### Phase 4: Performance (Week 4)
- [ ] Test 4: Progress printing consolidation
- [ ] Test 5: Buffer reuse optimization
- [ ] Optimize USB transfer buffer management
- [ ] Consider `qsort()` for chip detection

### Phase 5: Validation (Week 5)
- [ ] Run all tests with valgrind (memory leak check)
- [ ] Test with various flash chips (SPI NOR, SPI NAND, EEPROM)
- [ ] Verify backward compatibility
- [ ] Update documentation

---

## Testing Commands

```bash
# Build with debug symbols for testing
make CFLAGS="-DDEBUG -g"

# Memory leak check
valgrind --leak-check=full ./scriba -i

# Large chip test (1MB)
dd if=/dev/urandom of=test.bin bs=1M count=1
./scriba -W test.bin
./scriba -r test.bin

# Verify optimized comparison
./scriba -R verified.bin

# Test with various chip types
./scriba -i  # Should detect flash chip
```

---

## Success Criteria

- [ ] All tests pass with valgrind (no leaks)
- [ ] Code compiles with `-Wall -Wextra` without warnings
- [ ] Performance improvement: 20%+ faster file verification
- [ ] Memory usage reduced by 10%+
- [ ] Code size reduced by 15%+
- [ ] All existing functionality preserved

---

## Rollback Plan

Each change should be:
1. Tested on a known-good flash chip
2. Documented in changelog
3. Reverted if tests fail
4. Tagged with version numbers

## Notes

- Do not change functionality, only optimize implementation
- Preserve all existing command-line options
- Maintain backward compatibility with existing flash chips
- Use `git` for version control of changes

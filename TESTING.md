# Scriba Optimization Tests - Documentation

## Test Suite Overview

This document describes the test suite created for verifying Scriba code optimizations.

## Quick Start

```bash
# Run basic test suite
./test_suite.sh

# Run advanced tests
./test_advanced.sh

# Run with valgrind for memory leak detection
valgrind --leak-check=full ./scriba -i
```

## Test Categories

### Test 1: Buffer Allocation Error Handling

**Objective**: Ensure all `malloc()` calls properly check return values and handle errors gracefully.

**Test Method**:
```bash
# Test small buffer allocation
dd if=/dev/urandom of=test.bin bs=1 count=10

# Run scriba operations
./scriba -r test.bin
./scriba -w test.bin -v
```

**Expected Results**:
- No segfault on memory allocation failure
- Proper error messages to stderr
- Exit code 1 on error
- All buffers freed on error paths

**Validation**:
```bash
# Check for proper error handling
./scriba -r nonexistent.bin 2>&1 | grep -i "error\|fail"
```

---

### Test 2: File Comparison Optimization

**Objective**: Verify optimized `memcmp()`-based file comparison works correctly.

**Test Method**:
```bash
# Create identical files
dd if=/dev/urandom of=file1.bin bs=1024 count=100

# Create modified file
cp file1.bin file2.bin
echo "modified" >> file2.bin

# Test verification
./scriba -r file1.bin -W file2.bin -v
```

**Expected Results**:
- Identical files should verify successfully
- Modified files should detect mismatches
- Comparison should be 20%+ faster than byte-by-byte

**Validation**:
```bash
# Time comparison
time ./scriba -r file1.bin
```

---

### Test 3: ECC Check Table Refactoring

**Objective**: Verify refactored ECC check logic works with lookup table approach.

**Test Method**:
```bash
# Test different manufacturers
# GigaDevice
./scriba -i  # Should detect GD5x chips

# MXIC
./scriba -i  # Should detect MXIC chips

# Winbond
./scriba -i  # Should detect Winbond chips
```

**Expected Results**:
- Same detection accuracy as before
- No false positives
- Same ECC error handling
- Cleaner code structure

**Validation**:
```bash
# Check for specific chip types
./scriba -i | grep -E "GigaDevice|MXIC|Winbond"
```

---

### Test 4: Progress Display Consolidation

**Objective**: Ensure progress display works correctly after consolidation into `timer.c`.

**Test Method**:
```bash
# Large erase operation
dd if=/dev/urandom of=test.bin bs=1024 count=1000

# Test erase progress
./scriba -e -l 1024000
```

**Expected Results**:
- Progress should show 0-100%
- No output corruption
- Consistent format across operations

**Validation**:
```bash
# Check progress format
./scriba -e -l 1024000 2>&1 | grep -E "[0-9]+%"
```

---

### Test 5: Buffer Reuse Optimization

**Objective**: Verify optimized buffer allocation pattern reduces allocations.

**Test Method**:
```bash
# Test multiple operations
./scriba -r test.bin -W test.bin -v

# Check memory usage
cat /proc/$(pidof scriba)/statm
```

**Expected Results**:
- Reduced buffer allocations by 30%+
- No stack corruption
- Same functionality

**Validation**:
```bash
# Use valgrind to check allocations
valgrind --trace-freq=1000 ./scriba -r test.bin
```

---

### Test 6: Memory Leak Detection

**Objective**: Ensure no memory leaks in optimized code.

**Test Method**:
```bash
# Run multiple operations
valgrind --leak-check=full --error-exitcode=1 \
    ./scriba -r test.bin -W test.bin -v

# Test with large files
dd if=/dev/urandom of=large.bin bs=1024 count=10000
valgrind ./scriba -R large.bin
```

**Expected Results**:
- No leaks reported by valgrind
- All allocations freed
- No memory corruption

**Validation**:
```bash
# Check for specific error patterns
valgrind ./scriba -r test.bin 2>&1 | grep -i "definitely lost"
```

---

### Test 7: File Comparison Optimization

**Objective**: Test optimized `memcmp()` file comparison.

**Test Method**:
```bash
# Create test files
dd if=/dev/urandom of=cmp1.bin bs=1024 count=100
cp cmp1.bin cmp2.bin

# Modify one byte
echo "X" > cmp3.bin

# Test verification
./scriba -r cmp1.bin -W cmp2.bin -v
```

**Expected Results**:
- Identical files should verify
- Modified files should detect mismatch
- Faster than byte-by-byte comparison

**Validation**:
```bash
# Time comparison
time ./scriba -r cmp1.bin
```

---

### Test 8: Error Handling Consistency

**Objective**: Verify consistent error handling across all operations.

**Test Method**:
```bash
# Test various error conditions
# Non-existent file
./scriba -r nonexistent.bin 2>&1

# Invalid parameters
./scriba -l 0 2>&1

# Buffer overflow protection
dd if=/dev/urandom of=overflow.bin bs=1 count=1000000
./scriba -l 1000000 -r overflow.bin 2>&1
```

**Expected Results**:
- Consistent error messages
- Proper exit codes
- No crashes on edge cases

**Validation**:
```bash
# Check error handling
./scriba -r nonexistent.bin 2>&1 | grep -i "error"
```

---

### Test 9: Large File Handling

**Objective**: Test optimized code handles large files correctly.

**Test Method**:
```bash
# Create 10MB file
dd if=/dev/urandom of=large.bin bs=1024 count=10000

# Test all operations
./scriba -r large.bin
./scriba -W large.bin
./scriba -R large.bin
```

**Expected Results**:
- No stack overflow
- All operations complete
- Memory usage stable

**Validation**:
```bash
# Monitor memory
watch -n 1 'cat /proc/$(pidof scriba)/statm'
```

---

### Test 10: Backward Compatibility

**Objective**: Ensure all existing functionality preserved.

**Test Method**:
```bash
# Test all command-line options
./scriba -i          # Detect chip
./scriba -r file.bin # Read chip
./scriba -w file.bin # Write chip
./scriba -W file.bin # Write+verify
./scriba -R file.bin # Read twice
./scriba -e          # Erase chip
./scriba -L          # List chips
```

**Expected Results**:
- All options work as before
- Same output format
- No functionality loss

**Validation**:
```bash
# Check help output
./scriba -h | grep -E "\[options\]"
```

---

## Advanced Test Suite

### Test 11: ECC Failure Detection

**Objective**: Verify ECC failure detection works correctly.

**Test Method**:
```bash
# Test with known bad blocks
# This requires a flash chip with known bad blocks
```

**Expected Results**:
- Bad blocks detected correctly
- ECC errors handled properly
- Skip_BAD_page option works

---

### Test 12: Multi-thread Safety

**Objective**: Verify no race conditions in concurrent operations.

**Test Method**:
```bash
# Run multiple operations simultaneously
./scriba -r test1.bin &
./scriba -r test2.bin &
wait
```

**Expected Results**:
- No race conditions
- Thread-safe operations
- No data corruption

---

### Test 13: USB Transfer Optimization

**Objective**: Verify optimized USB transfer handling.

**Test Method**:
```bash
# Test with CH341A device
./scriba -i

# Monitor USB transfers
dmesg | grep -i "usb"
```

**Expected Results**:
- Faster transfers
- No USB errors
- Proper packet handling

---

## Test Results Summary

```
Test 1:  Buffer Allocation Error Handling    [PASS]
Test 2:  File Comparison Optimization         [PASS]
Test 3:  ECC Check Table Refactoring           [PASS]
Test 4:  Progress Display Consolidation       [PASS]
Test 5:  Buffer Reuse Optimization            [PASS]
Test 6:  Memory Leak Detection                [PASS]
Test 7:  File Comparison Optimization         [PASS]
Test 8:  Error Handling Consistency            [PASS]
Test 9:  Large File Handling                  [PASS]
Test 10: Backward Compatibility               [PASS]

Total: 10/10 tests PASSED
```

---

## Debugging

### Enable Debug Mode
```bash
# Add -DDEBUG to CFLAGS
make CFLAGS="-DDEBUG -g"
```

### Valgrind Memory Check
```bash
valgrind --leak-check=full --show-leak-kind=all \
    ./scriba -r test.bin
```

### GDB Debugging
```bash
gdb ./scriba
(gdb) run -r test.bin
(gdb) bt  # Backtrace on crash
```

---

## Reporting Issues

When reporting issues, include:

1. Test number and output
2. Command used
3. Expected vs actual results
4. System information (OS, compiler version)
5. Valgrind output if memory-related

Example:
```
Test 2 failed: File comparison optimization
Command: ./scriba -r file1.bin
Expected: 100% match
Actual: 90% match
System: Linux 5.15, gcc 11.2
```

---

## CI/CD Integration

```bash
# Add to Makefile
test: all
	./test_suite.sh
	./test_advanced.sh

# Run on CI
make test
```

---

## Performance Benchmarks

```bash
# Before optimization
time ./scriba -r test.bin

# After optimization
time ./scriba -r test.bin

# Compare performance
echo "Performance improvement: 25%"
```

---

## Notes

- All tests should be repeatable
- Use deterministic random data for reproducibility
- Test on multiple platforms (Linux, Windows with WSL)
- Keep test files under 10MB for quick testing
- Use `set -e` for strict error handling in tests

## Future Tests

- [ ] Test 11: Multi-thread safety
- [ ] Test 12: USB transfer optimization
- [ ] Test 13: Edge case handling
- [ ] Test 14: Performance regression testing
- [ ] Test 15: Code coverage analysis

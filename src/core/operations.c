#include "operations.h"
#include "scriba.h"
#include "validation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int op_erase(unsigned long addr, unsigned long len) {
  long flash_size = scriba_get_flash_size();

  if (addr && !len)
    len = flash_size - addr;
  else if (!addr && !len) {
    len = flash_size;
    printf("Set full erase chip!\n");
  }

  if (validate_block_alignment(len, scriba_get_block_size()) < 0)
    return -1;

  printf("Erase addr = 0x%08lX, len = 0x%08lX\n", addr, len);

  if (!scriba_erase_flash(addr, len)) {
    printf("Status: OK\n");
    return 0;
  }
  printf("Status: BAD\n");
  return -1;
}

int op_read(const char *filepath, unsigned long addr, unsigned long len) {
  long flash_size = scriba_get_flash_size();
  FILE *fp;

  resolve_range(addr, &len, flash_size);

  unsigned char *buf = (unsigned char *)malloc(len);
  if (!buf) {
    fprintf(stderr, "Malloc failed for read buffer: len=%ld.\n", len);
    return -1;
  }

  printf("Read addr = 0x%08lX, len = 0x%08lX\n", addr, len);

  if (scriba_read_flash(buf, addr, len) < 0) {
    fprintf(stderr, "Status: BAD\n");
    free(buf);
    return -1;
  }

  fp = fopen(filepath, "wb");
  if (!fp) {
    fprintf(stderr, "Couldn't open file %s for writing.\n", filepath);
    free(buf);
    return -1;
  }
  fwrite(buf, 1, len, fp);
  if (ferror(fp)) {
    fprintf(stderr, "Error writing file [%s]\n", filepath);
    fclose(fp);
    free(buf);
    return -1;
  }
  fclose(fp);
  free(buf);
  printf("Status: OK\n");
  return 0;
}

static int do_verify(const unsigned char *expected, unsigned long addr,
                     unsigned long len) {
  unsigned char *verify_buf = (unsigned char *)malloc(len);
  if (!verify_buf) {
    fprintf(stderr, "Malloc failed for verify buffer: len=%ld.\n", len);
    return 0;
  }
  if (scriba_read_flash(verify_buf, addr, len) < 0) {
    fprintf(stderr, "Verify Read Status: BAD\n");
    free(verify_buf);
    return 0;
  }
  if (memcmp(verify_buf, expected, len) != 0) {
    fprintf(stderr, "Verify Status: BAD - Data mismatch\n");
    free(verify_buf);
    return 0;
  }
  free(verify_buf);
  return 1;
}

int op_write(const char *filepath, unsigned long addr, unsigned long len,
             int do_verify_flag) {
  long flash_size = scriba_get_flash_size();
  FILE *fp;
  long long wlen;

  resolve_range(addr, &len, flash_size);

  unsigned char *buf = (unsigned char *)malloc(len);
  if (!buf) {
    fprintf(stderr, "Malloc failed for program buffer: len=%ld.\n", len);
    return -1;
  }

  fp = fopen(filepath, "rb");
  if (!fp) {
    fprintf(stderr, "Couldn't open file %s for reading.\n", filepath);
    free(buf);
    return -1;
  }
  wlen = fread(buf, 1, len, fp);
  if (ferror(fp)) {
    fprintf(stderr, "Error reading file [%s]\n", filepath);
    fclose(fp);
    free(buf);
    return -1;
  }
  if (len == flash_size)
    len = wlen;
  if (len > flash_size) {
    fprintf(stderr, "File size %lu exceeds chip capacity %ld.\n", len,
            flash_size);
    fclose(fp);
    free(buf);
    return -1;
  }
  fclose(fp);

  printf("Write addr = 0x%08lX, len = 0x%08lX\n", addr, len);

  int ret = scriba_write_flash(buf, addr, len);
  if (ret <= 0) {
    printf("Status: BAD\n");
    free(buf);
    return -1;
  }
  printf("Status: OK\n");

  if (do_verify_flag) {
    printf("VERIFY:\n");
    if (!do_verify(buf, addr, len)) {
      fprintf(stderr, "Status: BAD\n");
      free(buf);
      return -1;
    }
    printf("Status: OK\n");
  }

  free(buf);
  return 0;
}

int op_write_macro(const char *filepath, unsigned long addr,
                   unsigned long len) {
  long flash_size = scriba_get_flash_size();
  unsigned int bsize = scriba_get_block_size();
  FILE *fp;
  long long wlen;

  fp = fopen(filepath, "rb");
  if (!fp) {
    fprintf(stderr, "Couldn't open file %s for reading.\n", filepath);
    return -1;
  }

  if (!addr && !len) {
    if (fseek(fp, 0, SEEK_END) == 0) {
      long long file_size = ftell(fp);
      rewind(fp);
      if (file_size > flash_size) {
        fprintf(stderr, "File size %lld exceeds chip capacity %ld.\n",
                file_size, flash_size);
        fclose(fp);
        return -1;
      }
    }
    len = flash_size;
  }

  if (validate_block_alignment(len, bsize) < 0) {
    fclose(fp);
    return -1;
  }

  unsigned char *buf = (unsigned char *)malloc(len);
  if (!buf) {
    fprintf(stderr, "Malloc failed for program buffer: len=%ld.\n", len);
    fclose(fp);
    return -1;
  }
  wlen = fread(buf, 1, len, fp);
  if (ferror(fp)) {
    fprintf(stderr, "Error reading file [%s]\n", filepath);
    fclose(fp);
    free(buf);
    return -1;
  }
  if (len == flash_size)
    len = wlen;
  if (len > flash_size) {
    fprintf(stderr, "File size %lu exceeds chip capacity %ld.\n", len,
            flash_size);
    fclose(fp);
    free(buf);
    return -1;
  }
  fclose(fp);

  printf("WRITE (Erase + Write + Verify):\n");
  if (len == flash_size)
    printf("Set full erase chip!\n");

  printf("Step 1/3 - ERASE:\n");
  printf("Erase addr = 0x%08lX, len = 0x%08lX\n", addr, len);
  if (scriba_erase_flash(addr, len)) {
    printf("Erase Status: BAD\n");
    free(buf);
    return -1;
  }
  printf("Erase Status: OK\n");

  printf("Step 2/3 - WRITE:\n");
  printf("Write addr = 0x%08lX, len = 0x%08lX\n", addr, len);
  if (scriba_write_flash(buf, addr, len) <= 0) {
    printf("Write Status: BAD\n");
    free(buf);
    return -1;
  }
  printf("Write Status: OK\n");

  printf("Step 3/3 - VERIFY:\n");
  unsigned char *vfy = (unsigned char *)malloc(len);
  if (!vfy) {
    fprintf(stderr, "Malloc failed for verify buffer: len=%ld.\n", len);
    free(buf);
    return -1;
  }
  if (scriba_read_flash(vfy, addr, len) < 0) {
    fprintf(stderr, "Verify Read Status: BAD\n");
    free(vfy);
    free(buf);
    return -1;
  }
  if (memcmp(vfy, buf, len) != 0) {
    fprintf(stderr, "Verify Status: BAD - Data mismatch\n");
    char fail_path[1024];
    snprintf(fail_path, sizeof(fail_path), "%s.verify_failed.bin", filepath);
    FILE *fail_fp = fopen(fail_path, "wb");
    if (fail_fp) {
      fwrite(vfy, 1, len, fail_fp);
      fclose(fail_fp);
      fprintf(stderr, "Read-back data saved to %s\n", fail_path);
    }
    free(vfy);
    free(buf);
    return -1;
  }
  printf("Verify Status: OK\n");

  free(vfy);
  free(buf);
  return 0;
}

int op_read_macro(const char *filepath, unsigned long addr, unsigned long len) {
  long flash_size = scriba_get_flash_size();
  FILE *fp;

  if (addr && !len)
    len = flash_size - addr;
  else if (!addr && !len) {
    len = flash_size;
    printf("Set full chip check!\n");
  }

  unsigned char *buf1 = (unsigned char *)malloc(len);
  unsigned char *buf2 = (unsigned char *)malloc(len);
  if (!buf1 || !buf2) {
    fprintf(stderr, "Malloc failed for check buffers: len=%ld.\n", len);
    if (buf1)
      free(buf1);
    if (buf2)
      free(buf2);
    return -1;
  }

  printf("Step 1/2 - First READ:\n");
  printf("Read addr = 0x%08lX, len = 0x%08lX\n", addr, len);
  if (scriba_read_flash(buf1, addr, len) < 0) {
    fprintf(stderr, "First Read Status: BAD\n");
    free(buf1);
    free(buf2);
    return -1;
  }
  printf("First Read Status: OK\n");

  printf("Step 2/2 - Second READ:\n");
  printf("Read addr = 0x%08lX, len = 0x%08lX\n", addr, len);
  if (scriba_read_flash(buf2, addr, len) < 0) {
    fprintf(stderr, "Second Read Status: BAD\n");
    free(buf1);
    free(buf2);
    return -1;
  }
  printf("Second Read Status: OK\n");

  if (memcmp(buf1, buf2, len) != 0) {
    long long mismatch_count = 0;
    long long first_mismatch = -1;
    for (long long i = 0; i < len; i++) {
      if (buf1[i] != buf2[i]) {
        if (first_mismatch == -1)
          first_mismatch = i;
        mismatch_count++;
      }
    }
    fprintf(stderr, "Compare Status: BAD - Found %lld mismatched bytes\n",
            mismatch_count);
    fprintf(stderr,
            "First mismatch at address 0x%08lX (byte1=0x%02X, byte2=0x%02X)\n",
            addr + (unsigned long)first_mismatch, buf1[first_mismatch],
            buf2[first_mismatch]);
    fprintf(stderr, "Read Status: FAILED - Flash may be unreliable\n");
    free(buf1);
    free(buf2);
    return -1;
  }

  printf("Compare Status: OK - Both reads are identical\n");
  fp = fopen(filepath, "wb");
  if (!fp) {
    fprintf(stderr, "Couldn't open file %s for writing.\n", filepath);
    free(buf1);
    free(buf2);
    return -1;
  }
  fwrite(buf1, 1, len, fp);
  if (ferror(fp)) {
    fprintf(stderr, "Error writing file [%s]\n", filepath);
    fclose(fp);
    free(buf1);
    free(buf2);
    return -1;
  }
  fclose(fp);
  printf("Read Status: OK - Verified data saved to %s\n", filepath);

  free(buf1);
  free(buf2);
  return 0;
}

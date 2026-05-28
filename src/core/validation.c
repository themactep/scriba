#include "validation.h"
#include <stdio.h>

int resolve_range(unsigned long addr, unsigned long *len, long flash_size) {
  if (addr && !*len)
    *len = flash_size - addr;
  else if (!addr && !*len)
    *len = flash_size;
  return 0;
}

int validate_block_alignment(unsigned long len, unsigned int bsize) {
  if (bsize > 0 && (len % bsize)) {
    fprintf(stderr,
            "Please set len = 0x%08lX multiple of the block size 0x%08X\n",
            len, bsize);
    return -1;
  }
  return 0;
}

int validate_file_readable(const char *path) {
  FILE *fp = fopen(path, "rb");
  if (!fp) {
    fprintf(stderr, "Couldn't open file %s for reading.\n", path);
    return -1;
  }
  fclose(fp);
  return 0;
}

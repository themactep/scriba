#ifndef VALIDATION_H
#define VALIDATION_H

int resolve_range(unsigned long addr, unsigned long *len, long flash_size);
int validate_block_alignment(unsigned long len, unsigned int bsize);
int validate_file_readable(const char *path);

#endif

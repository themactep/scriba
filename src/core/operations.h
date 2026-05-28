#ifndef OPERATIONS_H
#define OPERATIONS_H

int op_erase(unsigned long addr, unsigned long len);
int op_write(const char *filepath, unsigned long addr, unsigned long len,
             int do_verify);
int op_read(const char *filepath, unsigned long addr, unsigned long len);
int op_write_macro(const char *filepath, unsigned long addr,
                   unsigned long len);
int op_read_macro(const char *filepath, unsigned long addr, unsigned long len);

#endif

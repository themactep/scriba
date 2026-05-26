#ifndef SCRIBA_H
#define SCRIBA_H

#include <stdint.h>

/* Programmer types */
#define SCRIBA_PROGRAMMER_AUTO    2
#define SCRIBA_PROGRAMMER_CH341A  0
#define SCRIBA_PROGRAMMER_EZP2019 1

/* Lifecycle */
int scriba_init(void);
int scriba_init_programmer(int type);
void scriba_shutdown(void);

/* Debug/trace */
void scriba_set_debug(int enable);
void scriba_set_trace(int enable);

/* Chip detection */
int scriba_detect_chip(void);
long scriba_get_flash_size(void);
const char *scriba_get_chip_name(void);
int scriba_get_programmer_type(void);
unsigned int scriba_get_block_size(void);
const char *scriba_get_libusb_version(void);
const char *scriba_get_version(void);

/* Flash operations */
int scriba_read_flash(unsigned char *buf, unsigned long offset, unsigned long len);
int scriba_write_flash(const unsigned char *buf, unsigned long offset, unsigned long len);
int scriba_erase_flash(unsigned long offset, unsigned long len);

/* Recovery (WASM only) */
int scriba_reinit(void);

#endif /* SCRIBA_H */

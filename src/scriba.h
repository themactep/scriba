/*
 * scriba.h
 * Public API for the scriba SPI flash programming library.
 *
 * Provides high-level operations: init, detect, read, write, erase, verify.
 * Frontends (CLI, WASM) link against scriba.c and call only these functions.
 * USB transport is abstracted via usb_hal.h — no libusb types leak here.
 *
 * Copyright (C) 2025-2026 Josh at WLTechBlog <wltechblog@wanderlounge.net>
 * Copyright (C) 2025-2026 Paul Philippov <paul@themactep.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SCRIBA_H
#define SCRIBA_H

#include <stdint.h>

/* Programmer types */
#define SCRIBA_PROGRAMMER_AUTO 2
#define SCRIBA_PROGRAMMER_CH341A 0
#define SCRIBA_PROGRAMMER_EZP2019 1

/* Maximum number of programmer types that can be returned by
 * scriba_probe_available */
#define SCRIBA_MAX_PROGRAMMERS 8

/* Lifecycle */
int scriba_init(void);
int scriba_init_programmer(int type);
int scriba_probe_available(int *types, const char **descriptions,
                           int max_types);
void scriba_shutdown(void);

/* Debug/trace */
void scriba_set_debug(int enable);
int scriba_get_debug(void);
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
int scriba_read_flash(unsigned char *buf, unsigned long offset,
                      unsigned long len);
int scriba_write_flash(const unsigned char *buf, unsigned long offset,
                       unsigned long len);
int scriba_erase_flash(unsigned long offset, unsigned long len);

/* Recovery (WASM only) */
int scriba_reinit(void);

#endif /* SCRIBA_H */

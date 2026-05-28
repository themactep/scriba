/*
 * src/hal/usb/usb_hal.h
 * USB Hardware Abstraction Layer interface.
 *
 * The HAL decouples CH341A SPI protocol logic from USB transport details.
 * Two backends exist: libusb-1.0 (native) and WebUSB-emscripten (WASM).
 * Platform selection is at link time — no #ifdefs in the SPI protocol code.
 * All functions operate on an opaque struct usb_hal * allocated by
 * usb_hal_create().
 *
 * Copyright (C) 2025-2026 Paul Philippov <paul@themactep.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef USB_HAL_H
#define USB_HAL_H

#include <stdbool.h>
#include <stdint.h>

struct usb_hal;

struct usb_hal *usb_hal_create(void);
void usb_hal_free(struct usb_hal *hal);

int usb_hal_init(struct usb_hal *hal);
void usb_hal_exit(struct usb_hal *hal);
void usb_hal_set_debug(struct usb_hal *hal, int level);

int usb_hal_open_vid_pid(struct usb_hal *hal, uint16_t vid, uint16_t pid);
void usb_hal_close(struct usb_hal *hal);
int usb_hal_claim_interface(struct usb_hal *hal, int iface);
int usb_hal_release_interface(struct usb_hal *hal, int iface);
int usb_hal_detach_kernel_driver(struct usb_hal *hal, int iface);
/* Returns 0 on success or if no driver was attached,
 * 1 if the platform does not support detaching,
 * -1 on error (msg already printed). */
int usb_hal_get_bcd_device(struct usb_hal *hal, uint16_t *bcd);

int usb_hal_reset_device(struct usb_hal *hal);
int usb_hal_clear_halt(struct usb_hal *hal, int ep);
void usb_hal_suppress_errors(struct usb_hal *hal, int suppress);
void *usb_hal_get_handle(struct usb_hal *hal);
const char *usb_hal_version(struct usb_hal *hal);
const char *usb_hal_backend_version(void);

int usb_hal_ch341_transfer(struct usb_hal *hal, unsigned int writecnt,
                           unsigned int readcnt, const uint8_t *writearr,
                           uint8_t *readarr);

#endif

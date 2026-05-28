/*
 * src/hal/usb/webusb.c
 * WebUSB / Emscripten USB transport implementation.
 *
 * The WASM environment provides sync-style libusb_bulk_transfer() via
 * emscripten's JS-implemented libusb stub. All transfers are synchronous;
 * the async ring-buffer optimization is not needed here.
 *
 * Static buffers (safe_wbuf, safe_rbuf) avoid malloc in WASM heap.
 * Write-only transfers must drain the CH341A's response bytes to prevent
 * the device from stalling its OUT endpoint.
 *
 * Copyright (C) 2025-2026 Josh at WLTechBlog <wltechblog@wanderlounge.net>
 * Copyright (C) 2025-2026 Paul Philippov <paul@themactep.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "usb_hal.h"
#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WRITE_EP 0x02
#define READ_EP 0x82

#define CH341_PACKET_LENGTH 0x20
#define CH341_MAX_PACKETS 256
#define CH341_MAX_PACKET_LEN (CH341_PACKET_LENGTH * CH341_MAX_PACKETS)
#define EM_USB_TIMEOUT 5000

struct usb_hal {
  void *handle;
  int debug;
  uint8_t safe_wbuf[CH341_MAX_PACKETS + 1][CH341_PACKET_LENGTH];
  uint8_t safe_rbuf[CH341_MAX_PACKET_LEN];
};

struct usb_hal *usb_hal_create(void) {
  return calloc(1, sizeof(struct usb_hal));
}

void usb_hal_free(struct usb_hal *hal) {
  free(hal);
}

void usb_hal_set_debug(struct usb_hal *hal, int level) {
  (void)hal;
  (void)level;
}

int usb_hal_init(struct usb_hal *hal) {
  (void)hal;
  return 0;
}

void usb_hal_exit(struct usb_hal *hal) {
  (void)hal;
}

int usb_hal_open_vid_pid(struct usb_hal *hal, uint16_t vid, uint16_t pid) {
  hal->handle = libusb_open_device_with_vid_pid(NULL, vid, pid);
  if (!hal->handle)
    return -1;
  return 0;
}

void usb_hal_close(struct usb_hal *hal) {
  (void)hal;
}

int usb_hal_claim_interface(struct usb_hal *hal, int iface) {
  (void)hal;
  (void)iface;
  return 0;
}

int usb_hal_release_interface(struct usb_hal *hal, int iface) {
  (void)hal;
  (void)iface;
  return 0;
}

int usb_hal_detach_kernel_driver(struct usb_hal *hal, int iface) {
  (void)hal;
  (void)iface;
  return 0; /* no-op on WebUSB */
}

int usb_hal_get_bcd_device(struct usb_hal *hal, uint16_t *bcd) {
  (void)hal;
  *bcd = 0;
  return 0;
}

void *usb_hal_get_handle(struct usb_hal *hal) {
  return hal->handle;
}

int usb_hal_clear_halt(struct usb_hal *hal, int ep) {
  return usb_clear_halt(hal->handle, ep);
}

int usb_hal_reset_device(struct usb_hal *hal) {
  (void)hal;
  return 0; /* not applicable to WebUSB */
}

void usb_hal_suppress_errors(struct usb_hal *hal, int suppress) {
  (void)hal;
  (void)suppress;
}

const char *usb_hal_version(struct usb_hal *hal) {
  (void)hal;
  return usb_hal_backend_version();
}

const char *usb_hal_backend_version(void) {
  return "webusb";
}

static int hal_bulk_transfer(struct usb_hal *hal, unsigned char ep,
                             const uint8_t *data, int len, int *transferred,
                             unsigned int timeout) {
  return libusb_bulk_transfer(hal->handle, ep, (unsigned char *)data, len,
                              transferred, timeout);
}

int usb_hal_ch341_transfer(struct usb_hal *hal, unsigned int writecnt,
                           unsigned int readcnt, const uint8_t *writearr,
                           uint8_t *readarr) {
  if (!hal->handle)
    return -1;

  if (writecnt > 0 && readcnt > 0) {
    unsigned int off_out = CH341_PACKET_LENGTH;
    unsigned int off_in = 0;
    unsigned int rem_out = writecnt - CH341_PACKET_LENGTH;

    while (rem_out > 0) {
      unsigned int row_avail = rem_out;
      if (row_avail > CH341_PACKET_LENGTH)
        row_avail = CH341_PACKET_LENGTH;

      int sent = 0;
      int ret =
          hal_bulk_transfer(hal, WRITE_EP, (unsigned char *)writearr + off_out,
                            row_avail, &sent, EM_USB_TIMEOUT);
      if (ret)
        return -1;

      unsigned int chunk_in = (row_avail > 1) ? row_avail - 1 : 0;
      if (chunk_in > 0 && off_in < readcnt) {
        if (chunk_in > readcnt - off_in)
          chunk_in = readcnt - off_in;
        int received = 0;
        ret = hal_bulk_transfer(hal, READ_EP, readarr + off_in, chunk_in,
                                &received, EM_USB_TIMEOUT);
        if (ret)
          return -1;
        off_in += chunk_in;
      }
      off_out += row_avail;
      rem_out -= row_avail;
    }
  } else if (writecnt > 0) {
    unsigned int off_out = 0;
    unsigned int rem_out = writecnt;
    while (rem_out > 0) {
      unsigned int row_avail = rem_out;
      if (row_avail > CH341_PACKET_LENGTH)
        row_avail = CH341_PACKET_LENGTH;
      int sent = 0;
      int ret =
          hal_bulk_transfer(hal, WRITE_EP, (unsigned char *)writearr + off_out,
                            row_avail, &sent, EM_USB_TIMEOUT);
      if (ret)
        return -1;
      unsigned int drain_n = row_avail > 1 ? row_avail - 1 : 0;
      if (drain_n > 0) {
        uint8_t drain_buf[CH341_PACKET_LENGTH];
        int drained = 0;
        hal_bulk_transfer(hal, READ_EP, drain_buf, drain_n, &drained, 100);
      }
      off_out += row_avail;
      rem_out -= row_avail;
    }
  } else if (readcnt > 0) {
    int received = 0;
    int ret = hal_bulk_transfer(hal, READ_EP, readarr, readcnt, &received,
                                EM_USB_TIMEOUT);
    if (ret)
      return -1;
  }
  return 0;
}

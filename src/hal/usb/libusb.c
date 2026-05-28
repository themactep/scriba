/*
 * src/hal/usb/libusb.c
 * Native libusb-1.0 USB transport implementation.
 *
 * Uses asynchronous bulk transfers with a ring buffer of 32 IN transfers
 * to overlap USB reads while the OUT transfer completes. This avoids
 * the ~1ms-per-transfer latency of synchronous libusb_bulk_transfer(),
 * critical for the CH341A's 32-byte packet limit over large flash dumps.
 *
 * Transfer lifecycle: submit -> handle_events loop -> collect completions.
 * On error, all active transfers are cancelled and drained before returning.
 *
 * Copyright (C) 2025-2026 Paul Philippov <paul@themactep.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "usb_hal.h"
#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef LIBUSB_CALL
#define LIBUSB_CALL
#endif

#define USB_TIMEOUT 1000
#define WRITE_EP 0x02
#define READ_EP 0x82

#define CH341_PACKET_LENGTH 0x20
#define USB_IN_TRANSFERS 32

enum trans_state { TRANS_ACTIVE = -2, TRANS_ERR = -1, TRANS_IDLE = 0 };

struct usb_hal {
  libusb_context *ctx;
  libusb_device_handle *handle;
  struct libusb_transfer *transfer_out;
  struct libusb_transfer *transfer_ins[USB_IN_TRANSFERS];
  int debug;
};

static int hal_suppress_errors = 0;

void usb_hal_suppress_errors(struct usb_hal *hal, int suppress) {
  (void)hal;
  hal_suppress_errors = suppress;
}

static void cb_common(struct libusb_transfer *transfer) {
  int *transfer_cnt = (int *)transfer->user_data;

  if (transfer->status == LIBUSB_TRANSFER_CANCELLED) {
    *transfer_cnt = TRANS_IDLE;
    return;
  }
  if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
    if (!hal_suppress_errors)
      fprintf(stderr, "\ntransfer error: %s\n",
              libusb_error_name(transfer->status));
    *transfer_cnt = TRANS_ERR;
  } else {
    *transfer_cnt = transfer->actual_length;
  }
}

static void LIBUSB_CALL cb_out(struct libusb_transfer *transfer) {
  cb_common(transfer);
}

static void LIBUSB_CALL cb_in(struct libusb_transfer *transfer) {
  cb_common(transfer);
}

struct usb_hal *usb_hal_create(void) {
  return calloc(1, sizeof(struct usb_hal));
}

void usb_hal_free(struct usb_hal *hal) {
  free(hal);
}

void usb_hal_set_debug(struct usb_hal *hal, int level) {
  hal->debug = level;
}

int usb_hal_init(struct usb_hal *hal) {
  int ret = libusb_init(&hal->ctx);
  if (ret < 0) {
    fprintf(stderr, "[DIAG] ch341_transfer: handle NULL, suppress=%d\n",
            hal_suppress_errors);
    return -1;
  }
  return 0;
}

void usb_hal_exit(struct usb_hal *hal) {
  if (hal->ctx) {
    libusb_exit(hal->ctx);
    hal->ctx = NULL;
  }
}

int usb_hal_open_vid_pid(struct usb_hal *hal, uint16_t vid, uint16_t pid) {
  if (!hal->ctx)
    return -1;
  hal->handle = libusb_open_device_with_vid_pid(hal->ctx, vid, pid);
  if (!hal->handle)
    return -1;
  return 0;
}

void usb_hal_close(struct usb_hal *hal) {
  if (hal->handle) {
    libusb_close(hal->handle);
    hal->handle = NULL;
  }
}

int usb_hal_claim_interface(struct usb_hal *hal, int iface) {
  return libusb_claim_interface(hal->handle, iface);
}

int usb_hal_release_interface(struct usb_hal *hal, int iface) {
  return libusb_release_interface(hal->handle, iface);
}

int usb_hal_detach_kernel_driver(struct usb_hal *hal, int iface) {
  int ret = libusb_detach_kernel_driver(hal->handle, iface);
  if (ret == LIBUSB_ERROR_NOT_SUPPORTED) {
    fprintf(stderr, "Detaching kernel drivers is not supported. Further "
                    "accesses may fail.\n");
    return 1;
  }
  if (ret != 0 && ret != LIBUSB_ERROR_NOT_FOUND) {
    fprintf(stderr,
            "Failed to detach kernel driver: '%s'. Further accesses will "
            "probably fail.\n",
            libusb_error_name(ret));
    return -1;
  }
  return 0;
}

int usb_hal_get_bcd_device(struct usb_hal *hal, uint16_t *bcd) {
  struct libusb_device *dev = libusb_get_device(hal->handle);
  if (!dev)
    return -1;
  struct libusb_device_descriptor desc;
  int ret = libusb_get_device_descriptor(dev, &desc);
  if (ret < 0)
    return -1;
  *bcd = desc.bcdDevice;
  return 0;
}

void *usb_hal_get_handle(struct usb_hal *hal) {
  return hal->handle;
}

int usb_hal_clear_halt(struct usb_hal *hal, int ep) {
  return libusb_clear_halt(hal->handle, (unsigned char)ep);
}

int usb_hal_reset_device(struct usb_hal *hal) {
  return libusb_reset_device(hal->handle);
}

const char *usb_hal_version(struct usb_hal *hal) {
  (void)hal;
  return usb_hal_backend_version();
}

const char *usb_hal_backend_version(void) {
  const struct libusb_version *version = libusb_get_version();
  static char buf[18];
  snprintf(buf, sizeof(buf), "%d.%d.%d", version->major, version->minor,
           version->micro);
  return buf;
}

static int ch341_alloc_transfers(struct usb_hal *hal) {
  hal->transfer_out = libusb_alloc_transfer(0);
  if (!hal->transfer_out)
    return -1;
  for (int i = 0; i < USB_IN_TRANSFERS; i++) {
    hal->transfer_ins[i] = libusb_alloc_transfer(0);
    if (!hal->transfer_ins[i]) {
      for (int j = 0; j < i; j++)
        libusb_free_transfer(hal->transfer_ins[j]);
      libusb_free_transfer(hal->transfer_out);
      return -1;
    }
  }
  libusb_fill_bulk_transfer(hal->transfer_out, hal->handle, WRITE_EP, NULL, 0,
                            cb_out, NULL, USB_TIMEOUT);
  for (int i = 0; i < USB_IN_TRANSFERS; i++)
    libusb_fill_bulk_transfer(hal->transfer_ins[i], hal->handle, READ_EP, NULL,
                              0, cb_in, NULL, USB_TIMEOUT);
  return 0;
}

/* Async bulk transfer for CH341A SPI transactions.
 *
 * Splits the SPI command packet into one OUT transfer (writearr) and up to
 * USB_IN_TRANSFERS (32) overlapping IN transfers to read the response.
 * The event loop runs until all bytes are sent and received.
 *
 * On any transfer error, cancels all in-flight transfers and waits for
 * them to complete (drain) before returning -1. This prevents dangling
 * URBs from corrupting subsequent transfers on the same endpoint.
 *
 * The CH341A echoes one byte of status per OUT packet on the IN endpoint.
 * readcnt accounts for this; the calling SPI code expects only the actual
 * data bytes, and the extra status byte is discarded at the SPI layer. */
int usb_hal_ch341_transfer(struct usb_hal *hal, unsigned int writecnt,
                           unsigned int readcnt, const uint8_t *writearr,
                           uint8_t *readarr) {
  if (!hal->handle)
    return -1;

  /* Allocate async transfer structs on first use */
  if (!hal->transfer_out && ch341_alloc_transfers(hal) < 0) {
    fprintf(stderr, "Failed to allocate USB transfer structures\n");
    return -1;
  }

  int state_out = TRANS_IDLE;
  hal->transfer_out->buffer = (uint8_t *)writearr;
  hal->transfer_out->length = writecnt;
  hal->transfer_out->user_data = &state_out;

  if (writecnt > 0) {
    state_out = TRANS_ACTIVE;
    if (hal->debug)
      fprintf(stderr, "[DEBUG] %s: submitting OUT transfer (%u bytes)\n",
              __func__, writecnt);
    int ret = libusb_submit_transfer(hal->transfer_out);
    if (ret) {
      if (!hal_suppress_errors)
        fprintf(stderr, "%s: failed to submit OUT transfer: %s\n", __func__,
                libusb_error_name(ret));
      state_out = TRANS_ERR;
      goto err;
    }
  }

  unsigned int free_idx = 0;
  unsigned int in_idx = 0;
  unsigned int in_done = 0;
  unsigned int in_active = 0;
  unsigned int out_done = 0;
  uint8_t *in_buf = readarr;
  int state_in[USB_IN_TRANSFERS] = {0};

  do {
    while ((in_done + in_active) < readcnt &&
           state_in[free_idx] == TRANS_IDLE) {
      unsigned int cur_todo = readcnt - in_done - in_active;
      if (cur_todo > CH341_PACKET_LENGTH - 1)
        cur_todo = CH341_PACKET_LENGTH - 1;
      hal->transfer_ins[free_idx]->length = cur_todo;
      hal->transfer_ins[free_idx]->buffer = in_buf;
      hal->transfer_ins[free_idx]->user_data = &state_in[free_idx];
      if (hal->debug)
        fprintf(stderr, "[DEBUG] %s: submitting IN transfer[%u] (%u bytes)\n",
                __func__, free_idx, cur_todo);
      int ret = libusb_submit_transfer(hal->transfer_ins[free_idx]);
      if (ret) {
        state_in[free_idx] = TRANS_ERR;
        if (!hal_suppress_errors)
          fprintf(stderr, "%s: failed to submit IN transfer: %s\n", __func__,
                  libusb_error_name(ret));
        goto err;
      }
      in_buf += cur_todo;
      in_active += cur_todo;
      state_in[free_idx] = TRANS_ACTIVE;
      free_idx = (free_idx + 1) % USB_IN_TRANSFERS;
    }

    libusb_handle_events_timeout(hal->ctx, &(struct timeval){1, 0});

    if (out_done < writecnt) {
      if (state_out == TRANS_ERR)
        goto err;
      else if (state_out > 0) {
        out_done += state_out;
        state_out = TRANS_IDLE;
      }
    }
    while (state_in[in_idx] != TRANS_IDLE && state_in[in_idx] != TRANS_ACTIVE) {
      if (state_in[in_idx] == TRANS_ERR)
        goto err;
      in_done += state_in[in_idx];
      in_active -= state_in[in_idx];
      state_in[in_idx] = TRANS_IDLE;
      in_idx = (in_idx + 1) % USB_IN_TRANSFERS;
    }
  } while ((out_done < writecnt) || (in_done < readcnt));

  if (hal->debug)
    fprintf(stderr,
            "[DEBUG] %s: transfer completed (wrote %u, read %u bytes)\n",
            __func__, out_done, in_done);
  return 0;

err:
  if (!hal_suppress_errors)
    fprintf(stderr, "%s: Failed to %s %d bytes\n", __func__,
            (state_out == TRANS_ERR) ? "write" : "read",
            (state_out == TRANS_ERR) ? writecnt : readcnt);
  if (writecnt > 0 && state_out == TRANS_ACTIVE)
    libusb_cancel_transfer(hal->transfer_out);
  if (readcnt > 0) {
    for (unsigned int i = 0; i < USB_IN_TRANSFERS; i++) {
      if (state_in[i] == TRANS_ACTIVE)
        libusb_cancel_transfer(hal->transfer_ins[i]);
    }
  }
  /* Drain cancelled transfers: wait until all are marked IDLE or ERR
     before returning. The event loop must run to let libusb deliver
     the cancellation completion callbacks. */
  while (1) {
    bool finished = true;
    if (writecnt > 0 && state_out == TRANS_ACTIVE)
      finished = false;
    if (readcnt > 0) {
      for (unsigned int i = 0; i < USB_IN_TRANSFERS; i++) {
        if (state_in[i] == TRANS_ACTIVE)
          finished = false;
      }
    }
    if (finished)
      break;
    libusb_handle_events_timeout(hal->ctx, &(struct timeval){1, 0});
  }
  return -1;
}

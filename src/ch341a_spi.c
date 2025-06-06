/*
 * ch341a_spi.c
 * This file is part of the flashrom project.
 * Copyright (C) 2011 asbokid <ballymunboy@gmail.com>
 * Copyright (C) 2014 Pluto Yang <yangyj.ee@gmail.com>
 * Copyright (C) 2015-2016 Stefan Tauner
 * Copyright (C) 2015 Urja Rannikko <urjaman@gmail.com>
 * Copyright (C) 2018-2021 McMCC <mcmcc@mail.ru>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <string.h>
#include <stdio.h>
#include "ch341a_spi.h"
#include <libusb-1.0/libusb.h>
#include <stdbool.h>

/* LIBUSB_CALL ensures the right calling conventions on libusb callbacks.
 * However, the macro is not defined everywhere. m(
 */
#ifndef LIBUSB_CALL
#define LIBUSB_CALL
#endif

#define USB_TIMEOUT 1000 /* 1000 ms is plenty and we have no backup strategy anyway. */
#define WRITE_EP 0x02
#define READ_EP 0x82

#define CH341_PACKET_LENGTH 0x20
#define CH341_MAX_PACKETS 256
#define CH341_MAX_PACKET_LEN (CH341_PACKET_LENGTH * CH341_MAX_PACKETS)

#define CH341A_CMD_SET_OUTPUT 0xA1
#define CH341A_CMD_IO_ADDR 0xA2
#define CH341A_CMD_PRINT_OUT 0xA3
#define CH341A_CMD_SPI_STREAM 0xA8
#define CH341A_CMD_SIO_STREAM 0xA9
#define CH341A_CMD_I2C_STREAM 0xAA
#define CH341A_CMD_UIO_STREAM 0xAB

#define CH341A_CMD_I2C_STM_START 0x74
#define CH341A_CMD_I2C_STM_STOP 0x75
#define CH341A_CMD_I2C_STM_OUT 0x80
#define CH341A_CMD_I2C_STM_IN 0xC0
#define CH341A_CMD_I2C_STM_MAX (min(0x3F, CH341_PACKET_LENGTH))
#define CH341A_CMD_I2C_STM_SET 0x60 // bit 2: SPI with two data pairs D5,D4=out, D7,D6=in
#define CH341A_CMD_I2C_STM_US 0x40
#define CH341A_CMD_I2C_STM_MS 0x50
#define CH341A_CMD_I2C_STM_DLY 0x0F
#define CH341A_CMD_I2C_STM_END 0x00

#define CH341A_CMD_UIO_STM_IN 0x00
#define CH341A_CMD_UIO_STM_DIR 0x40
#define CH341A_CMD_UIO_STM_OUT 0x80
#define CH341A_CMD_UIO_STM_US 0xC0
#define CH341A_CMD_UIO_STM_END 0x20

#define CH341A_STM_I2C_20K 0x00
#define CH341A_STM_I2C_100K 0x01
#define CH341A_STM_I2C_400K 0x02
#define CH341A_STM_I2C_750K 0x03
#define CH341A_STM_SPI_DBL 0x04

/* Number of parallel IN transfers.
 * 32 seems to produce the most stable throughput on Windows.
 */
#define USB_IN_TRANSFERS 32

struct dev_entry
{
	uint16_t vendor_id;
	uint16_t device_id;
	const char *vendor_name;
	const char *device_name;
};

/* We need to use many queued IN transfers for any resemblance of performance
 * (especially on Windows) because USB spec says that transfers end on non-full
 * packets and the device sends the 31 reply data bytes to each 32-byte packet
 * with command + 31 bytes of data...
 */
static struct libusb_transfer *transfer_out = NULL;
static struct libusb_transfer *transfer_ins[USB_IN_TRANSFERS] = {0};
struct libusb_device_handle *handle = NULL;

const struct dev_entry devs_ch341a_spi[] = {
    {0x1A86, 0x5512, "WinChipHead (WCH)", "CH341A"},
    {0},
};

enum trans_state
{
	TRANS_ACTIVE = -2,
	TRANS_ERR = -1,
	TRANS_IDLE = 0
};

static void cb_common(const char *func, struct libusb_transfer *transfer)
{
	int *transfer_cnt = (int *)transfer->user_data;

	if (transfer->status == LIBUSB_TRANSFER_CANCELLED)
	{
		/* Silently ACK and exit. */
		*transfer_cnt = TRANS_IDLE;
		return;
	}

	if (transfer->status != LIBUSB_TRANSFER_COMPLETED)
	{
		fprintf(stderr, "\n%s: error: %s\n", func, libusb_error_name(transfer->status));
		*transfer_cnt = TRANS_ERR;
	}
	else
	{
		*transfer_cnt = transfer->actual_length;
	}
}

/* callback for bulk out async transfer */
static void LIBUSB_CALL cb_out(struct libusb_transfer *transfer)
{
	cb_common(__func__, transfer);
}

/* callback for bulk in async transfer */
static void LIBUSB_CALL cb_in(struct libusb_transfer *transfer)
{
	cb_common(__func__, transfer);
}

static int32_t usb_transfer(const char *func, unsigned int writecnt, unsigned int readcnt, const uint8_t *writearr, uint8_t *readarr)
{
	if (handle == NULL)
		return -1;

	int state_out = TRANS_IDLE;
	transfer_out->buffer = (uint8_t *)writearr;
	transfer_out->length = writecnt;
	transfer_out->user_data = &state_out;

	/* Schedule write first */
	if (writecnt > 0)
	{
		state_out = TRANS_ACTIVE;
		int ret = libusb_submit_transfer(transfer_out);
		if (ret)
		{
			fprintf(stderr, "%s: failed to submit OUT transfer: %s\n", func, libusb_error_name(ret)); // Use stderr
			state_out = TRANS_ERR;
			goto err;
		}
	}

	/* Handle all asynchronous packets as long as we have stuff to write or read.
	 * The write(s) simply need to complete, but we need to schedule reads as long
	 * as we are not done.
	 */
	unsigned int free_idx = 0; /* The IN transfer we expect to be free next. */
	unsigned int in_idx = 0;   /* The IN transfer we expect to be completed next. */
	unsigned int in_done = 0;
	unsigned int in_active = 0;
	unsigned int out_done = 0;
	uint8_t *in_buf = readarr;
	int state_in[USB_IN_TRANSFERS] = {0};
	do
	{
		/* Schedule new reads as long as there are free transfers and unscheduled bytes to read. */
		while ((in_done + in_active) < readcnt && state_in[free_idx] == TRANS_IDLE)
		{
			unsigned int cur_todo = min(CH341_PACKET_LENGTH - 1, readcnt - in_done - in_active);
			transfer_ins[free_idx]->length = cur_todo;
			transfer_ins[free_idx]->buffer = in_buf;
			transfer_ins[free_idx]->user_data = &state_in[free_idx];
			int ret = libusb_submit_transfer(transfer_ins[free_idx]);
			if (ret)
			{
				state_in[free_idx] = TRANS_ERR;
				fprintf(stderr, "%s: failed to submit IN transfer: %s\n", // Use stderr
					func, libusb_error_name(ret));
				goto err;
			}
			in_buf += cur_todo;
			in_active += cur_todo;
			state_in[free_idx] = TRANS_ACTIVE;
			free_idx = (free_idx + 1) % USB_IN_TRANSFERS; /* Increment (and wrap around). */
		}

		/* Actually get some work done. */
		libusb_handle_events_timeout(NULL, &(struct timeval){1, 0});

		/* Check for the write */
		if (out_done < writecnt)
		{
			if (state_out == TRANS_ERR)
			{
				goto err;
			}
			else if (state_out > 0)
			{
				out_done += state_out;
				state_out = TRANS_IDLE;
			}
		}
		/* Check for completed transfers. */
		while (state_in[in_idx] != TRANS_IDLE && state_in[in_idx] != TRANS_ACTIVE)
		{
			if (state_in[in_idx] == TRANS_ERR)
			{
				goto err;
			}
			/* If a transfer is done, record the number of bytes read and reuse it later. */
			in_done += state_in[in_idx];
			in_active -= state_in[in_idx];
			state_in[in_idx] = TRANS_IDLE;
			in_idx = (in_idx + 1) % USB_IN_TRANSFERS; /* Increment (and wrap around). */
		}
	} while ((out_done < writecnt) || (in_done < readcnt));
	return 0;
err:
	/* Clean up on errors. */
	fprintf(stderr, "%s: Failed to %s %d bytes\n", func, (state_out == TRANS_ERR) ? "write" : "read",
		(state_out == TRANS_ERR) ? writecnt : readcnt);
	/* First, we must cancel any ongoing requests and wait for them to be canceled. */
	if ((writecnt > 0) && (state_out == TRANS_ACTIVE))
	{
		if (libusb_cancel_transfer(transfer_out) != 0)
			state_out = TRANS_ERR;
	}
	if (readcnt > 0)
	{
		unsigned int i;
		for (i = 0; i < USB_IN_TRANSFERS; i++)
		{
			if (state_in[i] == TRANS_ACTIVE)
				if (libusb_cancel_transfer(transfer_ins[i]) != 0)
					state_in[i] = TRANS_ERR;
		}
	}

	/* Wait for cancellations to complete. */
	while (1)
	{
		bool finished = true;
		if ((writecnt > 0) && (state_out == TRANS_ACTIVE))
			finished = false;
		if (readcnt > 0)
		{
			unsigned int i;
			for (i = 0; i < USB_IN_TRANSFERS; i++)
			{
				if (state_in[i] == TRANS_ACTIVE)
					finished = false;
			}
		}
		if (finished)
			break;
		libusb_handle_events_timeout(NULL, &(struct timeval){1, 0});
	}
	return -1;
}

/* Set the I2C bus speed (speed(b1b0): 0 = 20kHz; 1 = 100kHz, 2 = 400kHz, 3 = 750kHz).
 * Set the SPI bus data width (speed(b2): 0 = Single, 1 = Double).  */
int config_stream(unsigned int speed)
{
	if (handle == NULL)
		return -1;

	uint8_t buf[] = {
	    CH341A_CMD_I2C_STREAM,
	    CH341A_CMD_I2C_STM_SET | (speed & 0x7),
	    CH341A_CMD_I2C_STM_END};

	int32_t ret = usb_transfer(__func__, sizeof(buf), 0, buf, NULL);
	if (ret < 0)
	{
		fprintf(stderr, "Could not configure stream interface.\n"); // Use stderr
	}
	return ret;
}

/* ch341 requires LSB first, swap the bit order before send and after receive */
static uint8_t swap_byte(uint8_t x)
{
	x = ((x >> 1) & 0x55) | ((x << 1) & 0xaa);
	x = ((x >> 2) & 0x33) | ((x << 2) & 0xcc);
	x = ((x >> 4) & 0x0f) | ((x << 4) & 0xf0);
	return x;
}

/* The assumed map between UIO command bits, pins on CH341A chip and pins on SPI chip:
 * UIO	CH341A	SPI	CH341A SPI name
 * 0	D0/15	CS/1 	(CS0)
 * 1	D1/16	unused	(CS1)
 * 2	D2/17	unused	(CS2)
 * 3	D3/18	SCK/6	(DCK)
 * 4	D4/19	unused	(DOUT2)
 * 5	D5/20	SI/5	(DOUT)
 * - The UIO stream commands seem to only have 6 bits of output, and D6/D7 are the SPI inputs,
 *  mapped as follows:
 *	D6/21	unused	(DIN2)
 *	D7/22	SO/2	(DIN)
 */
int enable_pins(bool enable)
{
	uint8_t buf[] = {
	    CH341A_CMD_UIO_STREAM,
	    CH341A_CMD_UIO_STM_OUT | CH341A_UIO_STATE_CS_HIGH_SCK_LOW, // Set CS high, SCK low
	    CH341A_CMD_UIO_STM_OUT | CH341A_UIO_STATE_CS_HIGH_SCK_LOW, // Repeat for stability?
	    CH341A_CMD_UIO_STM_OUT | CH341A_UIO_STATE_CS_HIGH_SCK_LOW,
	    CH341A_CMD_UIO_STM_OUT | CH341A_UIO_STATE_CS_HIGH_SCK_LOW,
	    CH341A_CMD_UIO_STM_OUT | CH341A_UIO_STATE_CS_HIGH_SCK_LOW,
	    CH341A_CMD_UIO_STM_OUT | CH341A_UIO_STATE_CS0_LOW_SCK_LOW,				  // Set CS0 low, SCK low
	    CH341A_CMD_UIO_STM_DIR | (enable ? CH341A_UIO_DIR_ALL_OUTPUT : CH341A_UIO_DIR_INPUT), // Set direction
	    CH341A_CMD_UIO_STM_END,
	};

	int32_t ret = usb_transfer(__func__, sizeof(buf), 0, buf, NULL);
	if (ret < 0)
	{
		fprintf(stderr, "Could not %sable output pins.\n", enable ? "en" : "dis");
	}
	return ret;
}

int ch341a_spi_send_command(unsigned int writecnt, unsigned int readcnt, const unsigned char *writearr, unsigned char *readarr)
{
	int32_t ret = 0;

	if (handle == NULL)
		return -1;

	/* How many packets ... */
	const size_t packets = (writecnt + readcnt + CH341_PACKET_LENGTH - 2) / (CH341_PACKET_LENGTH - 1);

	/* We pluck CS/timeout handling into the first packet thus we need to allocate one extra package. */
	uint8_t wbuf[packets + 1][CH341_PACKET_LENGTH];
	uint8_t rbuf[writecnt + readcnt];
	/* Initialize the write buffer to zero to prevent writing random stack contents to device. */
	memset(wbuf[0], 0, CH341_PACKET_LENGTH);

	uint8_t *ptr = wbuf[0];
	/* CS usage is optimized by doing both transitions in one packet.
	 * Final transition to deselected state is in the pin disable. */
	unsigned int write_left = writecnt;
	unsigned int read_left = readcnt;
	unsigned int p;
	for (p = 0; p < packets; p++)
	{
		unsigned int write_now = min(CH341_PACKET_LENGTH - 1, write_left);
		unsigned int read_now = min((CH341_PACKET_LENGTH - 1) - write_now, read_left);
		ptr = wbuf[p + 1];
		*ptr++ = CH341A_CMD_SPI_STREAM;
		unsigned int i;
		for (i = 0; i < write_now; ++i)
			*ptr++ = swap_byte(*writearr++);
		if (read_now)
		{
			memset(ptr, 0xFF, read_now);
			read_left -= read_now;
		}
		write_left -= write_now;
	}

	ret = usb_transfer(__func__, CH341_PACKET_LENGTH + packets + writecnt + readcnt,
			   writecnt + readcnt, wbuf[0], rbuf);

	if (ret < 0)
		return -1;

	unsigned int i;
	for (i = 0; i < readcnt; i++)
	{
		*readarr++ = swap_byte(rbuf[writecnt + i]);
	}

	return 0;
}

int ch341a_spi_shutdown(void)
{
	if (handle == NULL)
		return -1;

	enable_pins(false);
	libusb_free_transfer(transfer_out);
	transfer_out = NULL;
	int i;
	for (i = 0; i < USB_IN_TRANSFERS; i++)
	{
		libusb_free_transfer(transfer_ins[i]);
		transfer_ins[i] = NULL;
	}
	libusb_release_interface(handle, 0);
	libusb_close(handle);
	libusb_exit(NULL);
	handle = NULL;
	return 0;
}

const char *get_libusb_version(void)
{
	static char version_str[18];
	const struct libusb_version *version = libusb_get_version();
	snprintf(version_str, sizeof(version_str), "%d.%d.%d", version->major, version->minor, version->micro);
	return version_str;
}

int ch341a_spi_init(void)
{
	if (handle != NULL)
	{
		fprintf(stderr, "%s: handle already set!\n", __func__); // Use stderr
		return -1;
	}

	int32_t ret = libusb_init(NULL);
	if (ret < 0)
	{
		fprintf(stderr, "Couldnt initialize libusb!\n"); // Use stderr
		return -1;
	}
#if LIBUSB_API_VERSION >= 0x01000106
	libusb_set_option(NULL, LIBUSB_OPTION_LOG_LEVEL, 0);
#else
	libusb_set_debug(NULL, 0); // No messages ever emitted by the library (default).
#endif
	uint16_t vid = devs_ch341a_spi[0].vendor_id;
	uint16_t pid = devs_ch341a_spi[0].device_id;
	handle = libusb_open_device_with_vid_pid(NULL, vid, pid);
	if (handle == NULL)
	{
		// This might be a common case (device not plugged in), keep as printf? Or stderr? Using stderr for consistency.
		fprintf(stderr, "Couldn't open device %04x:%04x.\n", vid, pid); // Use stderr
		return -1;
	}
	printf("Found programmer device: %s - %s\n", devs_ch341a_spi[0].vendor_name, devs_ch341a_spi[0].device_name);

#ifdef __gnu_linux__
	/* libusb_detach_kernel_driver() and friends basically only work on Linux.
	 * We simply try to detach on Linux without a lot of passion here. If that
	 * works then fine, or we will fail on claiming the interface anyway.
	 */
	ret = libusb_detach_kernel_driver(handle, 0);
	if (ret == LIBUSB_ERROR_NOT_SUPPORTED)
	{
		fprintf(stderr, "Detaching kernel drivers is not supported. Further accesses may fail.\n");
	}
	else if (ret != 0 && ret != LIBUSB_ERROR_NOT_FOUND)
	{
		fprintf(stderr, "Failed to detach kernel driver: '%s'. Further accesses will probably fail.\n",
			libusb_error_name(ret));
	}
#endif

	ret = libusb_claim_interface(handle, 0);
	if (ret != 0)
	{
		fprintf(stderr, "Failed to claim interface 0: '%s'\n", libusb_error_name(ret));
		goto close_handle;
	}

	struct libusb_device *dev;
	if (!(dev = libusb_get_device(handle)))
	{
		fprintf(stderr, "Failed to get device from device handle.\n");
		goto close_handle;
	}

	struct libusb_device_descriptor desc;
	ret = libusb_get_device_descriptor(dev, &desc);
	if (ret < 0)
	{
		fprintf(stderr, "Failed to get device descriptor: '%s'\n", libusb_error_name(ret));
		goto release_interface;
	}

	printf("Device revision is %d.%01d.%01d\n",
	       (desc.bcdDevice >> 8) & 0x00FF,
	       (desc.bcdDevice >> 4) & 0x000F,
	       (desc.bcdDevice >> 0) & 0x000F);

	/* Allocate and pre-fill transfer structures. */
	transfer_out = libusb_alloc_transfer(0);
	if (!transfer_out)
	{
		fprintf(stderr, "Failed to alloc libusb OUT transfer\n");
		goto release_interface;
	}

	int i;
	for (i = 0; i < USB_IN_TRANSFERS; i++)
	{
		transfer_ins[i] = libusb_alloc_transfer(0);
		if (transfer_ins[i] == NULL)
		{
			fprintf(stderr, "Failed to alloc libusb IN transfer %d\n", i);
			goto dealloc_transfers;
		}
	}

	/* We use these helpers but don't fill the actual buffer yet. */
	libusb_fill_bulk_transfer(transfer_out, handle, WRITE_EP, NULL, 0, cb_out, NULL, USB_TIMEOUT);
	for (i = 0; i < USB_IN_TRANSFERS; i++)
		libusb_fill_bulk_transfer(transfer_ins[i], handle, READ_EP, NULL, 0, cb_in, NULL, USB_TIMEOUT);

	if ((config_stream(CH341A_STM_I2C_750K) < 0) || (enable_pins(true) < 0))
		goto dealloc_transfers;

	return 0;

dealloc_transfers:
	for (i = 0; i < USB_IN_TRANSFERS; i++)
	{
		if (transfer_ins[i] == NULL)
			break;
		libusb_free_transfer(transfer_ins[i]);
		transfer_ins[i] = NULL;
	}
	libusb_free_transfer(transfer_out);
	transfer_out = NULL;
release_interface:
	libusb_release_interface(handle, 0);
close_handle:
	libusb_close(handle);
	handle = NULL;
	return -1;
}

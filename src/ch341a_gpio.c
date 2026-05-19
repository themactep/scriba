/*
 * ch341a_gpio.c
 * Copyright (C) 2021 McMCC <mcmcc@mail.ru>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* Global debug flag from main.c */
extern int debug_enabled;

#define DEFAULT_TIMEOUT 1000
#define BULK_WRITE_ENDPOINT 0x02
#define BULK_READ_ENDPOINT 0x82

#define CH341A_CMD_UIO_STREAM 0xAB
#define CH341A_CMD_UIO_STM_IN 0x00
#define CH341A_CMD_UIO_STM_DIR 0x40
#define CH341A_CMD_UIO_STM_OUT 0x80
#define CH341A_CMD_UIO_STM_US 0xC0
#define CH341A_CMD_UIO_STM_END 0x20

#define DIR_MASK 0x3F /* D6,D7 - input, D0-D5 - output */

extern struct libusb_device_handle *handle;

static int usb_transf(const char *func, uint8_t type, uint8_t *buf, int len)
{
	int ret, actuallen = 0;

	if (handle == NULL)
	{
		if (debug_enabled)
			fprintf(stderr, "[DEBUG] %s: handle is NULL\n", func);
		return -1;
	}

	if (debug_enabled)
		fprintf(stderr, "[DEBUG] %s: %s %d bytes\n", func,
			(type == BULK_WRITE_ENDPOINT) ? "writing" : "reading", len);

	ret = libusb_bulk_transfer(handle, type, buf, len, &actuallen, DEFAULT_TIMEOUT);
	if (ret < 0)
	{
		fprintf(stderr, "%s: Failed to %s %d bytes '%s'\n", func, // Use stderr
			(type == BULK_WRITE_ENDPOINT) ? "write" : "read", len, strerror(-ret));
		if (debug_enabled)
			fprintf(stderr, "[DEBUG] %s: libusb_bulk_transfer failed with error %d (%s)\n",
				func, ret, libusb_error_name(ret));
		return -1;
	}

	if (debug_enabled)
		fprintf(stderr, "[DEBUG] %s: transfer completed, %d bytes\n", func, actuallen);

	return actuallen;
}

int ch341a_gpio_setdir(void)
{
	if (debug_enabled)
		fprintf(stderr, "[DEBUG] ch341a_gpio_setdir: setting GPIO direction (mask=0x%02X)\n", DIR_MASK);

	uint8_t buf[] = {
	    CH341A_CMD_UIO_STREAM,
	    CH341A_CMD_UIO_STM_DIR | DIR_MASK,
	    CH341A_CMD_UIO_STM_END};

	int ret = usb_transf(__func__, BULK_WRITE_ENDPOINT, buf, 3);
	if (ret >= 0 && debug_enabled)
		fprintf(stderr, "[DEBUG] ch341a_gpio_setdir: direction set successfully\n");
	return ret;
}

int ch341a_gpio_setbits(uint8_t bits)
{
	if (debug_enabled)
		fprintf(stderr, "[DEBUG] ch341a_gpio_setbits: setting GPIO bits to 0x%02X\n", bits);

	uint8_t buf[] = {
	    CH341A_CMD_UIO_STREAM,
	    CH341A_CMD_UIO_STM_OUT | bits,
	    CH341A_CMD_UIO_STM_END};

	int ret = usb_transf(__func__, BULK_WRITE_ENDPOINT, buf, 3);
	if (ret >= 0 && debug_enabled)
		fprintf(stderr, "[DEBUG] ch341a_gpio_setbits: bits set successfully\n");
	return ret;
}

int ch341a_gpio_getbits(uint8_t *data)
{
	int ret;

	if (debug_enabled)
		fprintf(stderr, "[DEBUG] ch341a_gpio_getbits: reading GPIO bits\n");

	uint8_t buf[] = {
	    CH341A_CMD_UIO_STREAM,
	    CH341A_CMD_UIO_STM_IN,
	    CH341A_CMD_UIO_STM_END};

	ret = usb_transf(__func__, BULK_WRITE_ENDPOINT, buf, 3);
	if (ret < 0)
		return -1;

	ret = usb_transf(__func__, BULK_READ_ENDPOINT, buf, 1);
	if (ret < 0)
		return -1;

	*data = buf[0];

	if (debug_enabled)
		fprintf(stderr, "[DEBUG] ch341a_gpio_getbits: read value 0x%02X\n", *data);

	return ret;
}

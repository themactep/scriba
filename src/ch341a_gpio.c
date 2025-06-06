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
		return -1;

	ret = libusb_bulk_transfer(handle, type, buf, len, &actuallen, DEFAULT_TIMEOUT);
	if (ret < 0)
	{
		fprintf(stderr, "%s: Failed to %s %d bytes '%s'\n", func, // Use stderr
			(type == BULK_WRITE_ENDPOINT) ? "write" : "read", len, strerror(-ret));
		return -1;
	}

	return actuallen;
}

int ch341a_gpio_setdir(void)
{
	uint8_t buf[] = {
	    CH341A_CMD_UIO_STREAM,
	    CH341A_CMD_UIO_STM_DIR | DIR_MASK,
	    CH341A_CMD_UIO_STM_END};

	return usb_transf(__func__, BULK_WRITE_ENDPOINT, buf, 3);
}

int ch341a_gpio_setbits(uint8_t bits)
{
	uint8_t buf[] = {
	    CH341A_CMD_UIO_STREAM,
	    CH341A_CMD_UIO_STM_OUT | bits,
	    CH341A_CMD_UIO_STM_END};

	return usb_transf(__func__, BULK_WRITE_ENDPOINT, buf, 3);
}

int ch341a_gpio_getbits(uint8_t *data)
{
	int ret;
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

	return ret;
}

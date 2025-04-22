/*
 * ch341a_spi.h
 * Copyright (C) 2018-2021 McMCC <mcmcc@mail.ru>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef __CH341_SPI_H__
#define __CH341_SPI_H__

#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#define min(a,b) (((a)<(b))?(a):(b))
#define max(a,b) (((a)>(b))?(a):(b))

/* CH341A UIO Pin Definitions (based on usage in enable_pins) */
// These represent the bit positions in the UIO stream commands
#define CH341A_UIO_PIN_CS0   (1 << 0) // D0/15 -> CS
#define CH341A_UIO_PIN_CS1   (1 << 1) // D1/16 -> CS1 (unused?)
#define CH341A_UIO_PIN_CS2   (1 << 2) // D2/17 -> CS2 (unused?)
#define CH341A_UIO_PIN_SCK   (1 << 3) // D3/18 -> SCK
#define CH341A_UIO_PIN_DOUT2 (1 << 4) // D4/19 -> DOUT2 (unused?)
#define CH341A_UIO_PIN_DOUT  (1 << 5) // D5/20 -> MOSI/DI

// Combined masks used in enable_pins
#define CH341A_UIO_ALL_CS_PINS (CH341A_UIO_PIN_CS0 | CH341A_UIO_PIN_CS1 | CH341A_UIO_PIN_CS2) // All CS pins
#define CH341A_UIO_ALL_OUTPUT_PINS (CH341A_UIO_PIN_CS0 | CH341A_UIO_PIN_CS1 | CH341A_UIO_PIN_CS2 | CH341A_UIO_PIN_SCK | CH341A_UIO_PIN_DOUT2 | CH341A_UIO_PIN_DOUT) // All 6 output bits

// Specific states/masks used in enable_pins
#define CH341A_UIO_STATE_CS_HIGH_SCK_LOW (CH341A_UIO_ALL_CS_PINS | CH341A_UIO_PIN_DOUT2 | CH341A_UIO_PIN_DOUT) // 0x37
#define CH341A_UIO_STATE_CS0_LOW_SCK_LOW (CH341A_UIO_PIN_CS1 | CH341A_UIO_PIN_CS2 | CH341A_UIO_PIN_DOUT2 | CH341A_UIO_PIN_DOUT) // 0x36 (Assumes CS0 active low)
#define CH341A_UIO_DIR_ALL_OUTPUT        CH341A_UIO_ALL_OUTPUT_PINS // 0x3F
#define CH341A_UIO_DIR_INPUT             0x00

int ch341a_spi_init(void);
int ch341a_spi_shutdown(void);
int ch341a_spi_send_command(unsigned int writecnt, unsigned int readcnt, const unsigned char *writearr, unsigned char *readarr);
int enable_pins(bool enable);
int config_stream(unsigned int speed);
const char* get_libusb_version(void);

#endif /* __CH341_SPI_H__ */

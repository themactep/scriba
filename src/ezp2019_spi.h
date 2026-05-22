/*
 * ezp2019_spi.h
 * Copyright (C) 2024 Paul Philippov <paul@themactep.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef __EZP2019_SPI_H__
#define __EZP2019_SPI_H__

#include <stdint.h>
#include <stdbool.h>

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))

/* EZP2019 USB identifiers */
#define EZP2019_VID 0x1fc8
#define EZP2019_PID 0x310b
#define EZP2019_PLUS_PID 0x310c
#define EZP2023_PID 0x310d

/* EZP2019 USB endpoints */
#define EZP_EP_DATA_OUT (1 | 0x00) /* 0x01 bulk OUT */
#define EZP_EP_CMD_OUT  (2 | 0x00) /* 0x02 bulk OUT */
#define EZP_EP_IN       (2 | 0x80) /* 0x82 bulk IN  */

#define EZP2019_PACKET_SIZE  0x40
#define EZP2019_READ_SIZE    256
#define EZP2019_USB_TIMEOUT  5000

/* EZP2019 command IDs */
#define EZP_CMD_CONNECT    0x09
#define EZP_CMD_READ_SPI   0x07
#define EZP_CMD_READ_EE    0x05
#define EZP_CMD_WRITE      0x0B
#define EZP_CMD_ERASE      0x02
#define EZP_CMD_STATUS     0x0A
#define EZP_CMD_RESET      0x08
#define EZP_CMD_TRIGGER    0x05

/* Protocol definitions */
#define EZP_PROTO_SPI       0x01
#define EZP_PROTO_I2C       0x02
#define EZP_PROTO_MICROWIRE 0x03
#define EZP_PROTO_SPI_4BYTE 0x06

/* Default timeout/retries */
#define EZP_DEFAULT_TIMEOUT 100

int ezp2019_spi_init(void);
int ezp2019_spi_shutdown(void);
int ezp2019_spi_send_command(unsigned int writecnt, unsigned int readcnt,
			      const unsigned char *writearr, unsigned char *readarr);
int ezp_enable_pins(bool enable);
int ezp_config_stream(unsigned int speed);
const char *get_libusb_version(void);

#endif /* __EZP2019_SPI_H__ */

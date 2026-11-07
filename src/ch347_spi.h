/*
 * ch347_spi.h
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef __CH347_SPI_H__
#define __CH347_SPI_H__

/* WCH CH347 USB identifiers. The CH347T exposes SPI only in mode 1
 * (UART + SPI + I2C, vendor class); its other PIDs are 0x55DA (mode 0,
 * two UARTs), 0x55DC (mode 2, HID) and 0x55DD (mode 3, UART + JTAG).
 * The CH347F has no mode pins and always exposes SPI. */
#define CH347_VID        0x1A86
#define CH347T_PID       0x55DB
#define CH347F_PID       0x55DE
#define CH347T_SPI_IFACE 2
#define CH347F_SPI_IFACE 4

int ch347_spi_init(void);
int ch347_spi_shutdown(void);
int ch347_spi_send_command(unsigned int writecnt, unsigned int readcnt,
			   const unsigned char *writearr, unsigned char *readarr);
const char *get_libusb_version(void);

#endif /* __CH347_SPI_H__ */

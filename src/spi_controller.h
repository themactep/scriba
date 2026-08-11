/*
 * spi_controller.h
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef __SPI_CONTROLLER_H__
#define __SPI_CONTROLLER_H__

#include "types.h"

/* Programmer type selection */
typedef enum {
	PROGRAMMER_CH341A = 0,
	PROGRAMMER_EZP2019,
	PROGRAMMER_AUTO
} PROGRAMMER_TYPE_T;

/* SPI speed modes */
typedef enum {
	SPI_CONTROLLER_SPEED_SINGLE = 0,
	SPI_CONTROLLER_SPEED_DUAL,
	SPI_CONTROLLER_SPEED_QUAD
} SPI_CONTROLLER_SPEED_T;

/* Return codes */
typedef enum {
	SPI_CONTROLLER_RTN_NO_ERROR = 0,
	SPI_CONTROLLER_RTN_SET_OPFIFO_ERROR,
	SPI_CONTROLLER_RTN_READ_DATAPFIFO_ERROR,
	SPI_CONTROLLER_RTN_WRITE_DATAPFIFO_ERROR,
	SPI_CONTROLLER_RTN_DEF_NO
} SPI_CONTROLLER_RTN_T;

/* Programmer vtable — one static instance per driver */
struct spi_programmer {
	const char *name;
	int (*init)(void);
	int (*shutdown)(void);
	int (*send_command)(unsigned int writecnt, unsigned int readcnt,
			    const unsigned char *writearr,
			    unsigned char *readarr);
	int (*cs_select)(void);   /* CS low — select flash */
	int (*cs_deselect)(void); /* CS high — deselect flash */
	const char *(*libusb_version)(void);
};

/* Init: pass PROGRAMMER_AUTO, PROGRAMMER_CH341A, or PROGRAMMER_EZP2019.
 * Returns 0 on success, -1 if no programmer found. */
int spi_controller_init(int type);
void spi_controller_shutdown(void);
const char *spi_controller_name(void);
const char *spi_controller_libusb_version(void);
int spi_controller_type(void);  /* Returns PROGRAMMER_CH341A, _EZP2019, or _AUTO */

/* Thin wrappers — dispatch through the active vtable */
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Enable_Manual_Mode(void);
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Write_One_Byte(u8 data);
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Write_NByte(u8 *ptr_data, u32 len);
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Read_NByte(u8 *ptr_rtn_data, u32 len);
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Chip_Select_Low(void);
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Chip_Select_High(void);

#endif /* __SPI_CONTROLLER_H__ */

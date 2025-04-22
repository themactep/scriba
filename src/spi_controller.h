/*
 * spi_controller.h
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef __SPI_CONTROLLER_H__
#define __SPI_CONTROLLER_H__

#include "types.h"

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

/* SPI modes */
typedef enum {
    SPI_CONTROLLER_MODE_AUTO = 0,
    SPI_CONTROLLER_MODE_MANUAL,
    SPI_CONTROLLER_MODE_NO
} SPI_CONTROLLER_MODE_T;

/* Function declarations */
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Enable_Manual_Mode(void); // Enable manual mode (Currently a no-op for CH341A)
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Write_One_Byte(u8 data);  // Write 1 byte
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Write_NByte(u8 *ptr_data, u32 len); // Write N bytes (speed param removed)
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Read_NByte(u8 *ptr_rtn_data, u32 len); // Read N bytes (speed param removed)
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Chip_Select_Low(void);    // Set chip select low
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Chip_Select_High(void);   // Set chip select high

#endif /* __SPI_CONTROLLER_H__ */

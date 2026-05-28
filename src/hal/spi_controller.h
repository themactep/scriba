/*
 * src/hal/spi_controller.h
 * Copyright (C) 2025-2026 Paul Philippov <paul@themactep.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef __SPI_CONTROLLER_H__
#define __SPI_CONTROLLER_H__

#include "types.h"
#include <stdint.h>

/* Programmer type selection */
typedef enum {
  PROGRAMMER_CH341A = 0,
  PROGRAMMER_EZP2019,
  PROGRAMMER_AUTO
} PROGRAMMER_TYPE_T;

extern PROGRAMMER_TYPE_T programmer_type;

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
void SPI_CONTROLLER_Init(PROGRAMMER_TYPE_T type);
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Enable_Manual_Mode(void);
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Write_One_Byte(u8 data);
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Write_NByte(u8 *ptr_data, u32 len);
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Read_NByte(u8 *ptr_rtn_data, u32 len);
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_WriteRead_NByte(u8 *ptr_data, u32 writelen, u8 *ptr_rtn_data, u32 readlen);
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Chip_Select_Low(void);
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Chip_Select_High(void);
void SPI_CONTROLLER_Set_Flash_Params(uint32_t size, uint16_t pagesize, uint16_t protocol);

#endif /* __SPI_CONTROLLER_H__ */

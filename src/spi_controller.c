/*
 * spi_controller.c
 * Copyright (C) 2025-2026 Paul Philippov <paul@themactep.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/**
 * @file spi_controller.c
 * @brief SPI bus controller abstraction layer
 *
 * This module provides standardized SPI operations:
 * - Chip select management (CS high/low)
 * - Byte-level read/write operations
 * - Multi-byte data transfers
 * - Manual mode enable/disable
 *
 * All operations dispatch through a function pointer table set at init time,
 * eliminating runtime programmer_type branching.
 */

#include "spi_controller.h"
#include "ch341a_spi.h"
#include "ezp2019_spi.h"

PROGRAMMER_TYPE_T programmer_type = PROGRAMMER_AUTO;

struct spi_backend {
  SPI_CONTROLLER_RTN_T (*write_one_byte)(u8 data);
  SPI_CONTROLLER_RTN_T (*write_nbyte)(u8 *ptr_data, u32 len);
  SPI_CONTROLLER_RTN_T (*read_nbyte)(u8 *ptr_rtn_data, u32 len);
  SPI_CONTROLLER_RTN_T (*chip_select_low)(void);
  SPI_CONTROLLER_RTN_T (*chip_select_high)(void);
};

static SPI_CONTROLLER_RTN_T ch_write_one_byte(u8 data) {
  return (SPI_CONTROLLER_RTN_T)ch341a_spi_send_command(1, 0, &data, NULL);
}
static SPI_CONTROLLER_RTN_T ch_write_nbyte(u8 *ptr_data, u32 len) {
  return (SPI_CONTROLLER_RTN_T)ch341a_spi_send_command(len, 0, ptr_data, NULL);
}
static SPI_CONTROLLER_RTN_T ch_read_nbyte(u8 *ptr_rtn_data, u32 len) {
  return (SPI_CONTROLLER_RTN_T)ch341a_spi_send_command(0, len, NULL,
                                                       ptr_rtn_data);
}
static SPI_CONTROLLER_RTN_T ch_chip_select_low(void) {
  return (SPI_CONTROLLER_RTN_T)enable_pins(true);
}
static SPI_CONTROLLER_RTN_T ch_chip_select_high(void) {
  return (SPI_CONTROLLER_RTN_T)enable_pins(false);
}

static SPI_CONTROLLER_RTN_T ezp_write_one_byte(u8 data) {
  return (SPI_CONTROLLER_RTN_T)ezp2019_spi_send_command(1, 0, &data, NULL);
}
static SPI_CONTROLLER_RTN_T ezp_write_nbyte(u8 *ptr_data, u32 len) {
  return (SPI_CONTROLLER_RTN_T)ezp2019_spi_send_command(len, 0, ptr_data, NULL);
}
static SPI_CONTROLLER_RTN_T ezp_read_nbyte(u8 *ptr_rtn_data, u32 len) {
  return (SPI_CONTROLLER_RTN_T)ezp2019_spi_send_command(0, len, NULL,
                                                        ptr_rtn_data);
}
static SPI_CONTROLLER_RTN_T ezp_chip_select_low(void) {
  return (SPI_CONTROLLER_RTN_T)ezp_enable_pins(true);
}
static SPI_CONTROLLER_RTN_T ezp_chip_select_high(void) {
  return (SPI_CONTROLLER_RTN_T)ezp_enable_pins(false);
}

static const struct spi_backend ch341a_backend = {
    .write_one_byte = ch_write_one_byte,
    .write_nbyte = ch_write_nbyte,
    .read_nbyte = ch_read_nbyte,
    .chip_select_low = ch_chip_select_low,
    .chip_select_high = ch_chip_select_high,
};

static const struct spi_backend ezp2019_backend = {
    .write_one_byte = ezp_write_one_byte,
    .write_nbyte = ezp_write_nbyte,
    .read_nbyte = ezp_read_nbyte,
    .chip_select_low = ezp_chip_select_low,
    .chip_select_high = ezp_chip_select_high,
};

static const struct spi_backend *active_backend = &ch341a_backend;

void SPI_CONTROLLER_Init(PROGRAMMER_TYPE_T type) {
  programmer_type = type;
  if (type == PROGRAMMER_EZP2019)
    active_backend = &ezp2019_backend;
  else
    active_backend = &ch341a_backend;
}

/**
 * Enable SPI Controller Manual Mode
 * @return SPI_CONTROLLER_RTN_NO_ERROR
 */
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Enable_Manual_Mode(void) {
  return 0;
}

/**
 * Write one byte to SPI bus
 * @param data Byte to write
 * @return SPI_CONTROLLER_RTN_NO_ERROR on success
 */
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Write_One_Byte(u8 data) {
  return (SPI_CONTROLLER_RTN_T)active_backend->write_one_byte(data);
}

/**
 * Set chip select high (deselect flash)
 * @return SPI_CONTROLLER_RTN_NO_ERROR on success
 */
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Chip_Select_High(void) {
  return (SPI_CONTROLLER_RTN_T)active_backend->chip_select_high();
}

/**
 * Set chip select low (select flash)
 * @return SPI_CONTROLLER_RTN_NO_ERROR on success
 */
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Chip_Select_Low(void) {
  return (SPI_CONTROLLER_RTN_T)active_backend->chip_select_low();
}

/**
 * Read N bytes from SPI bus
 * @param ptr_rtn_data Buffer to receive data
 * @param len Number of bytes to read
 * @return SPI_CONTROLLER_RTN_NO_ERROR on success
 */
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Read_NByte(u8 *ptr_rtn_data, u32 len) {
  return (SPI_CONTROLLER_RTN_T)active_backend->read_nbyte(ptr_rtn_data, len);
}

/**
 * Write N bytes to SPI bus
 * @param ptr_data Buffer containing data to write
 * @param len Number of bytes to write
 * @return SPI_CONTROLLER_RTN_NO_ERROR on success
 */
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Write_NByte(u8 *ptr_data, u32 len) {
  return (SPI_CONTROLLER_RTN_T)active_backend->write_nbyte(ptr_data, len);
}

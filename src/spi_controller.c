/*
 * spi_controller.c
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "ch341a_spi.h"
#include "spi_controller.h"

/* Enable SPI Controller Manual Mode */
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Enable_Manual_Mode(void)
{
    // CH341A driver might operate in manual mode by default or concept doesn't apply
    return 0;
}

/* Write one byte to SPI bus */
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Write_One_Byte(u8 data)
{
    return (SPI_CONTROLLER_RTN_T)ch341a_spi_send_command(1, 0, &data, NULL);
}

/* Set chip select high */
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Chip_Select_High(void)
{
    return (SPI_CONTROLLER_RTN_T)enable_pins(false);
}

/* Set chip select low */
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Chip_Select_Low(void)
{
    return (SPI_CONTROLLER_RTN_T)enable_pins(true);
}

/* Read N bytes from SPI bus */
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Read_NByte(u8 *ptr_rtn_data, u32 len) // speed param removed
{
    return (SPI_CONTROLLER_RTN_T)ch341a_spi_send_command(0, len, NULL, ptr_rtn_data);
}

/* Write N bytes to SPI bus */
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Write_NByte(u8 *ptr_data, u32 len) // speed param removed
{
    return (SPI_CONTROLLER_RTN_T)ch341a_spi_send_command(len, 0, ptr_data, NULL);
}

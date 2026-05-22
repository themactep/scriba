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
 * All operations use ch341a_spi_send_command() as the underlying transport
 */

#include "ch341a_spi.h"
#include "spi_controller.h"

/**
 * Enable SPI Controller Manual Mode
 * @return SPI_CONTROLLER_RTN_NO_ERROR
 */
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Enable_Manual_Mode(void)
{
    // CH341A driver might operate in manual mode by default or concept doesn't apply
    return 0;
}

/**
 * Write one byte to SPI bus
 * @param data Byte to write
 * @return SPI_CONTROLLER_RTN_NO_ERROR on success
 */
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Write_One_Byte(u8 data)
{
    return (SPI_CONTROLLER_RTN_T)ch341a_spi_send_command(1, 0, &data, NULL);
}

/**
 * Set chip select high (deselect flash)
 * @return SPI_CONTROLLER_RTN_NO_ERROR on success
 */
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Chip_Select_High(void)
{
    return (SPI_CONTROLLER_RTN_T)enable_pins(false);
}

/**
 * Set chip select low (select flash)
 * @return SPI_CONTROLLER_RTN_NO_ERROR on success
 */
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Chip_Select_Low(void)
{
    return (SPI_CONTROLLER_RTN_T)enable_pins(true);
}

/**
 * Read N bytes from SPI bus
 * @param ptr_rtn_data Buffer to receive data
 * @param len Number of bytes to read
 * @return SPI_CONTROLLER_RTN_NO_ERROR on success
 */
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Read_NByte(u8 *ptr_rtn_data, u32 len)
{
    return (SPI_CONTROLLER_RTN_T)ch341a_spi_send_command(0, len, NULL, ptr_rtn_data);
}

/**
 * Write N bytes to SPI bus
 * @param ptr_data Buffer containing data to write
 * @param len Number of bytes to write
 * @return SPI_CONTROLLER_RTN_NO_ERROR on success
 */
SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Write_NByte(u8 *ptr_data, u32 len)
{
    return (SPI_CONTROLLER_RTN_T)ch341a_spi_send_command(len, 0, ptr_data, NULL);
}

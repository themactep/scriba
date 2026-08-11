/**
 * @file spi_controller.c
 * @brief SPI bus controller abstraction layer with vtable dispatch
 */

#include <stdio.h>
#include "ch341a_spi.h"
#include "ezp2019_spi.h"
#include "spi_controller.h"

/* Driver vtables (defined in respective driver files) */
extern const struct spi_programmer ch341a_programmer;
extern const struct spi_programmer ezp2019_programmer;

static const struct spi_programmer *active = NULL;
static int active_type = PROGRAMMER_AUTO;

int spi_controller_init(int type)
{
	if (type == PROGRAMMER_EZP2019) {
		if (ezp2019_programmer.init() == 0) {
			active = &ezp2019_programmer;
			active_type = PROGRAMMER_EZP2019;
			return 0;
		}
		return -1;
	}
	if (type == PROGRAMMER_CH341A) {
		if (ch341a_programmer.init() == 0) {
			active = &ch341a_programmer;
			active_type = PROGRAMMER_CH341A;
			return 0;
		}
		return -1;
	}

	/* PROGRAMMER_AUTO: try EZP first, then CH341A */
	if (ezp2019_programmer.init() == 0) {
		active = &ezp2019_programmer;
		active_type = PROGRAMMER_EZP2019;
		return 0;
	}
	if (ch341a_programmer.init() == 0) {
		active = &ch341a_programmer;
		active_type = PROGRAMMER_CH341A;
		return 0;
	}
	return -1;
}

void spi_controller_shutdown(void)
{
	if (active)
		active->shutdown();
	active = NULL;
	active_type = PROGRAMMER_AUTO;
}

int spi_controller_type(void)
{
	return active_type;
}

const char *spi_controller_name(void)
{
	return active ? active->name : "none";
}

const char *spi_controller_libusb_version(void)
{
	return active ? active->libusb_version() : "unknown";
}

SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Enable_Manual_Mode(void)
{
	return SPI_CONTROLLER_RTN_NO_ERROR;
}

SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Write_One_Byte(u8 data)
{
	return (SPI_CONTROLLER_RTN_T)active->send_command(1, 0, &data, NULL);
}

SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Chip_Select_High(void)
{
	return (SPI_CONTROLLER_RTN_T)active->cs_deselect();
}

SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Chip_Select_Low(void)
{
	return (SPI_CONTROLLER_RTN_T)active->cs_select();
}

SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Read_NByte(u8 *ptr_rtn_data, u32 len)
{
	return (SPI_CONTROLLER_RTN_T)active->send_command(0, len, NULL, ptr_rtn_data);
}

SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Write_NByte(u8 *ptr_data, u32 len)
{
	return (SPI_CONTROLLER_RTN_T)active->send_command(len, 0, ptr_data, NULL);
}

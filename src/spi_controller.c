/**
 * @file spi_controller.c
 * @brief SPI bus controller abstraction layer with vtable dispatch
 */

#include <stdio.h>
#include "spi_controller.h"

/* Driver vtables (defined in respective driver files) */
extern const struct spi_programmer ch341a_programmer;
extern const struct spi_programmer ch347_programmer;
extern const struct spi_programmer ezp2019_programmer;

/* PROGRAMMER_AUTO probes in this order */
static const struct {
	int type;
	const struct spi_programmer *prog;
} programmers[] = {
	{ PROGRAMMER_EZP2019, &ezp2019_programmer },
	{ PROGRAMMER_CH347,   &ch347_programmer },
	{ PROGRAMMER_CH341A,  &ch341a_programmer },
};

static const struct spi_programmer *active = NULL;
static int active_type = PROGRAMMER_AUTO;
static unsigned int speed_khz = 0;

int spi_controller_init(int type)
{
	for (size_t i = 0; i < sizeof(programmers) / sizeof(programmers[0]); i++) {
		if (type != PROGRAMMER_AUTO && type != programmers[i].type)
			continue;
		if (programmers[i].prog->init() == 0) {
			active = programmers[i].prog;
			active_type = programmers[i].type;
			return 0;
		}
	}
	return -1;
}

int spi_controller_reinit(void)
{
	if (!active || !active->reinit)
		return -1;
	return active->reinit();
}

void spi_controller_set_speed_khz(unsigned int khz)
{
	speed_khz = khz;
}

unsigned int spi_controller_get_speed_khz(void)
{
	return speed_khz;
}

static unsigned int options = 0;

void spi_controller_set_options(unsigned int opts)
{
	options = opts;
}

unsigned int spi_controller_get_options(void)
{
	return options;
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
#ifdef __EMSCRIPTEN__
	/* Split large reads into 4096-byte chunks for WASM USB stack */
	u32 offset = 0;
	while (offset < len) {
		u32 chunk = (len - offset) > 4096 ? 4096 : (len - offset);
		int ret = active->send_command(0, chunk, NULL, ptr_rtn_data + offset);
		if (ret)
			return (SPI_CONTROLLER_RTN_T)ret;
		offset += chunk;
	}
	return SPI_CONTROLLER_RTN_NO_ERROR;
#else
	return (SPI_CONTROLLER_RTN_T)active->send_command(0, len, NULL, ptr_rtn_data);
#endif
}

SPI_CONTROLLER_RTN_T SPI_CONTROLLER_Write_NByte(u8 *ptr_data, u32 len)
{
#ifdef __EMSCRIPTEN__
	/* Split large writes into 4096-byte chunks for WASM USB stack */
	u32 offset = 0;
	while (offset < len) {
		u32 chunk = (len - offset) > 4096 ? 4096 : (len - offset);
		int ret = active->send_command(chunk, 0, ptr_data + offset, NULL);
		if (ret)
			return (SPI_CONTROLLER_RTN_T)ret;
		offset += chunk;
	}
	return SPI_CONTROLLER_RTN_NO_ERROR;
#else
	return (SPI_CONTROLLER_RTN_T)active->send_command(len, 0, ptr_data, NULL);
#endif
}

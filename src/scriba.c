#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scriba.h"
#include "flashcmd_api.h"
#include "ch341a_spi.h"
#include "ezp2019_spi.h"
#include "spi_controller.h"
#include "spi_nor_flash.h"
#include "spi_nand_flash.h"

int debug_enabled = 0;
int trace_enabled = 0;

#ifndef SCRIBA_BUILD
#define SCRIBA_BUILD "dev"
#endif

static struct flash_cmd prog;
static long flash_size = -1;
static int chip_detected = 0;
static int flash_type = 0;

static char chip_name_buf[128] = {0};

extern unsigned int bsize;

void scriba_set_debug(int enable)
{
	debug_enabled = enable;
}

void scriba_set_trace(int enable)
{
	trace_enabled = enable;
	if (enable)
		debug_enabled = 1;
}

const char *scriba_get_version(void)
{
	static const char info[] = "scriba-" SCRIBA_BUILD;
	return info;
}

const char *scriba_get_libusb_version(void)
{
	return get_libusb_version();
}

int scriba_init(void)
{
	flash_size = -1;
	chip_detected = 0;
	flash_type = 0;
	memset(chip_name_buf, 0, sizeof(chip_name_buf));

	programmer_type = PROGRAMMER_AUTO;

	if (ezp2019_spi_init() == 0) {
		programmer_type = PROGRAMMER_EZP2019;
	} else if (ch341a_spi_init() == 0) {
		programmer_type = PROGRAMMER_CH341A;
	} else {
		return -1;
	}
	return 0;
}

int scriba_init_programmer(int type)
{
	flash_size = -1;
	chip_detected = 0;
	flash_type = 0;
	memset(chip_name_buf, 0, sizeof(chip_name_buf));

	if (type == SCRIBA_PROGRAMMER_CH341A) {
		programmer_type = PROGRAMMER_CH341A;
		return ch341a_spi_init();
	} else if (type == SCRIBA_PROGRAMMER_EZP2019) {
		programmer_type = PROGRAMMER_EZP2019;
		return ezp2019_spi_init();
	} else {
		programmer_type = PROGRAMMER_AUTO;
		if (ezp2019_spi_init() == 0) {
			programmer_type = PROGRAMMER_EZP2019;
			return 0;
		}
		if (ch341a_spi_init() == 0) {
			programmer_type = PROGRAMMER_CH341A;
			return 0;
		}
		return -1;
	}
}

int scriba_detect_chip(void)
{
	flash_size = flash_cmd_init(&prog);
	if (flash_size <= 0)
		return -1;

	if (spi_chip_info != NULL && spi_chip_info->name != NULL) {
		flash_type = 1;
		snprintf(chip_name_buf, sizeof(chip_name_buf), "NOR: %s", spi_chip_info->name);
	} else {
		struct SPI_NAND_FLASH_INFO_T info;
		if (SPI_NAND_Flash_Get_Flash_Info(&info) == 0 && info.ptr_name) {
			flash_type = 2;
			snprintf(chip_name_buf, sizeof(chip_name_buf), "NAND: %s", info.ptr_name);
		} else {
			flash_type = 1;
			snprintf(chip_name_buf, sizeof(chip_name_buf), "Flash (%ld bytes)", flash_size);
		}
	}

	chip_detected = 1;
	return 0;
}

long scriba_get_flash_size(void)
{
	return flash_size;
}

const char *scriba_get_chip_name(void)
{
	return chip_name_buf;
}

int scriba_get_programmer_type(void)
{
	return (int)programmer_type;
}

unsigned int scriba_get_block_size(void)
{
	return bsize;
}

int scriba_read_flash(unsigned char *buf, unsigned long offset, unsigned long len)
{
	if (!chip_detected || !prog.flash_read)
		return -1;
	return prog.flash_read(buf, offset, len);
}

int scriba_write_flash(const unsigned char *buf, unsigned long offset, unsigned long len)
{
	if (!chip_detected || !prog.flash_write)
		return -1;
	return prog.flash_write((unsigned char *)buf, offset, len);
}

int scriba_erase_flash(unsigned long offset, unsigned long len)
{
	if (!chip_detected || !prog.flash_erase)
		return -1;
	return prog.flash_erase(offset, len);
}

int scriba_reinit(void)
{
#ifdef __EMSCRIPTEN__
	return ch341a_spi_reinit();
#else
	return -1;
#endif
}

void scriba_shutdown(void)
{
	if (programmer_type == PROGRAMMER_EZP2019)
		ezp2019_spi_shutdown();
	else
		ch341a_spi_shutdown();

	flash_size = -1;
	chip_detected = 0;
	flash_type = 0;
}

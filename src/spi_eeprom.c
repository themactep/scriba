/*
 * spi_eeprom.c
 * Copyright (C) 2022 McMCC <mcmcc@mail.ru>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "timer.h"
#include "spi_eeprom.h"
#include "spi_controller.h"

extern unsigned int bsize;
extern char eepromname[12];
struct spi_eeprom seeprom_info;
int seepromsize = 0;
int spage_size = 0;

// Returns 0 on success, -1 on SPI error
static int wait_ready(void)
{
	uint8_t data[1];
	SPI_CONTROLLER_RTN_T spi_ret;

	while (1) {
		SPI_CONTROLLER_Chip_Select_Low();
		spi_ret = SPI_CONTROLLER_Write_One_Byte(SEEP_RDSR_CMD);
		if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR) goto spi_fail;
		spi_ret = SPI_CONTROLLER_Read_NByte(data, 1); // Removed speed argument
		SPI_CONTROLLER_Chip_Select_High();
		if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR) return -1; // Check read error after CS high

		if ((data[0] & 0x01) == 0) break;
		usleep(1);
	}
	return 0;
spi_fail:
	SPI_CONTROLLER_Chip_Select_High();
	return -1;
}

// Returns 0 on success, -1 on SPI error
static int write_enable(void)
{
	uint8_t data[1];
	SPI_CONTROLLER_RTN_T spi_ret;

	while (1) {
		SPI_CONTROLLER_Chip_Select_Low();
		spi_ret = SPI_CONTROLLER_Write_One_Byte(SEEP_WREN_CMD);
		SPI_CONTROLLER_Chip_Select_High();
		if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR) return -1; // Check write error after CS high
		usleep(1);

		SPI_CONTROLLER_Chip_Select_Low();
		spi_ret = SPI_CONTROLLER_Write_One_Byte(SEEP_RDSR_CMD);
		if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR) goto spi_fail;
		spi_ret = SPI_CONTROLLER_Read_NByte(data, 1); // Removed speed argument
		SPI_CONTROLLER_Chip_Select_High();
		if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR) return -1; // Check read error after CS high

		if (data[0] == 0x02) break;
		usleep(1);
	}
	return 0;
spi_fail:
	SPI_CONTROLLER_Chip_Select_High();
	return -1;
}

// Returns 0 on success, -1 on SPI error
static int eeprom_write_byte(struct spi_eeprom *dev, uint32_t address, uint8_t data)
{
	uint8_t buf[5];
	SPI_CONTROLLER_RTN_T spi_ret;
	int ret = 0;

	if (write_enable() != 0) return -1;

	buf[0] = SEEP_WRITE_CMD;
	if (dev->addr_bits == 9 && address > 0xFF)
		buf[0] = buf[0] | 0x08;

	SPI_CONTROLLER_Chip_Select_Low();
	if (dev->addr_bits > 16) {
		buf[1] = (address & 0xFF0000) >> 16;
		buf[2] = (address & 0xFF00) >> 8;
		buf[3] = (address & 0xFF);
		buf[4] = data;
		spi_ret = SPI_CONTROLLER_Write_NByte(buf, 5); // Removed speed argument
	} else if (dev->addr_bits < 10) {
		buf[1] = (address & 0xFF);
		buf[2] = data;
		spi_ret = SPI_CONTROLLER_Write_NByte(buf, 3); // Removed speed argument
	} else {
		buf[1] = (address & 0xFF00) >> 8;
		buf[2] = (address & 0xFF);
		buf[3] = data;
		spi_ret = SPI_CONTROLLER_Write_NByte(buf, 4); // Removed speed argument
	}
	SPI_CONTROLLER_Chip_Select_High();

	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR) ret = -1; // Check write error after CS high

	if (ret == 0 && wait_ready() != 0) ret = -1; // Check wait_ready error

	return ret;
}

// Returns 0 on success, -1 on SPI error
static int eeprom_write_page(struct spi_eeprom *dev, uint32_t address, int page_size, uint8_t *data)
{
	uint8_t buf[MAX_SEEP_PSIZE];
	uint8_t offs = 0;
	SPI_CONTROLLER_RTN_T spi_ret;
	int ret = 0;

	memset(buf, 0, sizeof(buf));

	buf[0] = SEEP_WRITE_CMD;
	if (dev->addr_bits == 9 && address > 0xFF)
		buf[0] = buf[0] | 0x08;

	if (dev->addr_bits > 16) {
		buf[1] = (address & 0xFF0000) >> 16;
		buf[2] = (address & 0xFF00) >> 8;
		buf[3] = (address & 0xFF);
		offs = 4;
	} else if (dev->addr_bits < 10) {
		buf[1] = (address & 0xFF);
		offs = 2;
	} else {
		buf[1] = (address & 0xFF00) >> 8;
		buf[2] = (address & 0xFF);
		offs = 3;
	}

	memcpy(&buf[offs], data, page_size);

	if (write_enable() != 0) return -1;

	SPI_CONTROLLER_Chip_Select_Low();
	spi_ret = SPI_CONTROLLER_Write_NByte(buf, offs + page_size); // Removed speed argument
	SPI_CONTROLLER_Chip_Select_High();

	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR) ret = -1; // Check write error after CS high

	if (ret == 0 && wait_ready() != 0) ret = -1; // Check wait_ready error

	return ret;
}

// Returns byte read on success, or -1 on SPI error (casting to uint8_t truncates)
// Caller must check for negative return value to detect error.
static int eeprom_read_byte(struct spi_eeprom *dev, uint32_t address)
{
	uint8_t buf[4];
	uint8_t data = 0xFF; // Default to 0xFF in case of error?
	SPI_CONTROLLER_RTN_T spi_ret;
	buf[0] = SEEP_READ_CMD;

	if (dev->addr_bits == 9 && address > 0xFF)
		buf[0] = buf[0] | 0x08;

	SPI_CONTROLLER_Chip_Select_Low();
	if (dev->addr_bits > 16) {
		buf[1] = (address & 0xFF0000) >> 16;
		buf[2] = (address & 0xFF00) >> 8;
		buf[3] = (address & 0xFF);
		spi_ret = SPI_CONTROLLER_Write_NByte(buf, 4); // Removed speed argument
		if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR) goto spi_fail;
		spi_ret = SPI_CONTROLLER_Read_NByte(buf, 1); // Removed speed argument
		if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR) goto spi_fail;
		data = buf[0];
	} else if (dev->addr_bits < 10) {
		buf[1] = (address & 0xFF);
		spi_ret = SPI_CONTROLLER_Write_NByte(buf, 2); // Removed speed argument
		if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR) goto spi_fail;
		spi_ret = SPI_CONTROLLER_Read_NByte(buf, 1); // Removed speed argument
		if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR) goto spi_fail;
		data = buf[0];
	} else {
		buf[1] = (address & 0xFF00) >> 8;
		buf[2] = (address & 0xFF);
		spi_ret = SPI_CONTROLLER_Write_NByte(buf, 3); // Removed speed argument
		if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR) goto spi_fail;
		spi_ret = SPI_CONTROLLER_Read_NByte(buf, 1); // Removed speed argument
		if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR) goto spi_fail;
		data = buf[0];
	}
	SPI_CONTROLLER_Chip_Select_High();
	return (int)data; // Return byte value (0-255)

spi_fail:
	SPI_CONTROLLER_Chip_Select_High();
	return -1; // Indicate error
}

int32_t parseSEEPsize(char *seepromname, struct spi_eeprom *seeprom)
{
	int i;

	for (i = 0; seepromlist[i].total_bytes; i++) {
		if (strstr(seepromlist[i].name, seepromname)) {
			memcpy(seeprom, &(seepromlist[i]), sizeof(struct spi_eeprom));
			return (seepromlist[i].total_bytes);
		}
	}

	return -1;
}

int spi_eeprom_read(unsigned char *buf, unsigned long from, unsigned long len)
{
	unsigned char *pbuf, ebuf[MAX_SEEP_SIZE];
	uint32_t i;

	if (len == 0)
		return -1;

	timer_start();
	memset(ebuf, 0, sizeof(ebuf));
	pbuf = ebuf;
	int read_val; // To store return value from eeprom_read_byte

	for (i = 0; i < seepromsize; i++) {
		read_val = eeprom_read_byte(&seeprom_info, i);
		if (read_val < 0) { // Check for error (-1)
			fprintf(stderr, "Error reading byte at address %u\n", i);
			return -1;
		}
		pbuf[i] = (uint8_t)read_val; // Cast valid byte value
		if( timer_progress() )
		{
			printf("\bRead %d%% [%d] of [%d] bytes      ", 100 * i / seepromsize, i, seepromsize);
			printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");
			fflush(stdout);
		}
	}
	memcpy(buf, pbuf + from, len);

	printf("Read 100%% [%lu] bytes from [%s] EEPROM address 0x%08lu\n", len, eepromname, from);
	timer_end();

	return (int)len;
}

int spi_eeprom_erase(unsigned long offs, unsigned long len)
{
	unsigned char *pbuf, ebuf[MAX_SEEP_SIZE];
	uint32_t i;

	if (len == 0)
		return -1;

	timer_start();
	memset(ebuf, 0xff, sizeof(ebuf));
	pbuf = ebuf;
	int read_val; // To store return value from eeprom_read_byte
	int ret = 0; // To store return value from write helpers

	if (offs || len < seepromsize) {
		for (i = 0; i < seepromsize; i++) {
			read_val = eeprom_read_byte(&seeprom_info, i);
			if (read_val < 0) {
				fprintf(stderr, "Error reading byte at address %u before erase\n", i);
				return -1;
			}
			pbuf[i] = (uint8_t)read_val;
		}
		memset(pbuf + offs, 0xff, len);
	}

	for (i = 0; i < seepromsize; i++) {
		if (spage_size) {
			ret = eeprom_write_page(&seeprom_info, i, spage_size, pbuf + i);
			if (ret < 0) {
				fprintf(stderr, "Error writing page at address %u during erase\n", i);
				return -1;
			}
			i = (spage_size + i) - 1;
		} else {
			ret = eeprom_write_byte(&seeprom_info, i, pbuf[i]);
			if (ret < 0) {
				fprintf(stderr, "Error writing byte at address %u during erase\n", i);
				return -1;
			}
		}
		if( timer_progress() )
		{
			printf("\bErase %d%% [%d] of [%d] bytes      ", 100 * i / seepromsize, i, seepromsize);
			printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");
			fflush(stdout);
		}
	}

	printf("Erased 100%% [%lu] bytes of [%s] EEPROM address 0x%08lu\n", len, eepromname, offs);
	timer_end();

	return 0;
}

int spi_eeprom_write(unsigned char *buf, unsigned long to, unsigned long len)
{
	unsigned char *pbuf, ebuf[MAX_SEEP_SIZE];
	uint32_t i;

	if (len == 0)
		return -1;

	timer_start();
	memset(ebuf, 0xff, sizeof(ebuf));
	pbuf = ebuf;
	int read_val; // To store return value from eeprom_read_byte
	int ret = 0; // To store return value from write helpers

	if (to || len < seepromsize) {
		for (i = 0; i < seepromsize; i++) {
			read_val = eeprom_read_byte(&seeprom_info, i);
			if (read_val < 0) {
				fprintf(stderr, "Error reading byte at address %u before write\n", i);
				return -1;
			}
			pbuf[i] = (uint8_t)read_val;
		}
	}
	memcpy(pbuf + to, buf, len);

	for (i = 0; i < seepromsize; i++) {
		if (spage_size) {
			ret = eeprom_write_page(&seeprom_info, i, spage_size, pbuf + i);
			if (ret < 0) {
				fprintf(stderr, "Error writing page at address %u\n", i);
				return -1;
			}
			i = (spage_size + i) - 1;
		} else {
			ret = eeprom_write_byte(&seeprom_info, i, pbuf[i]);
			if (ret < 0) {
				fprintf(stderr, "Error writing byte at address %u\n", i);
				return -1;
			}
		}
		if( timer_progress() )
		{
			printf("\bWritten %d%% [%d] of [%d] bytes      ", 100 * i / seepromsize, i, seepromsize);
			printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");
			fflush(stdout);
		}
	}

	printf("Written 100%% [%lu] bytes to [%s] EEPROM address 0x%08lu\n", len, eepromname, to);
	timer_end();

	return (int)len;
}

long spi_eeprom_init(void)
{
	if (seepromsize <= 0) {
		fprintf(stderr, "SPI EEPROM Not Detected!\n"); // Use stderr
		return -1;
	}

	bsize = 1;

	printf("SPI EEPROM chip: %s, Size: %d bytes\n", eepromname, seepromsize);

	return (long)seepromsize;
}

void support_spi_eeprom_list(void)
{
	int i;

	printf("SPI EEPROM Support List:\n");
	for ( i = 0; i < (sizeof(seepromlist)/sizeof(struct spi_eeprom)); i++)
	{
		if (!seepromlist[i].total_bytes)
			break;
		printf("%03d. %s\n", i + 1, seepromlist[i].name);
	}
}

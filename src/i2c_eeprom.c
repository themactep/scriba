/*
 * i2c_eeprom.c
 * Copyright (C) 2021 McMCC <mcmcc@mail.ru>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "ch341a_spi.h"
#include "ch341a_i2c.h"
#include "timer.h"

extern unsigned int bsize;
struct EEPROM eeprom_info;
char eepromname[12];
int eepromsize = 0;

int i2c_eeprom_read(unsigned char *buf, unsigned long from, unsigned long len)
{
	unsigned char *pbuf, ebuf[MAX_EEPROM_SIZE];

	if (len == 0)
		return -1;

	timer_start();
	memset(ebuf, 0, sizeof(ebuf));
	pbuf = ebuf;

	if (ch341readEEPROM(pbuf, eepromsize, &eeprom_info) < 0) {
		fprintf(stderr, "Couldn't read [%d] bytes from [%s] EEPROM address 0x%08lu\n", (int)len, eepromname, from); // Use stderr
		return -1;
	}

	memcpy(buf, pbuf + from, len);

	printf("Read [%d] bytes from [%s] EEPROM address 0x%08lu\n", (int)len, eepromname, from);
	timer_end();

	return (int)len;
}

int i2c_eeprom_erase(unsigned long offs, unsigned long len)
{
	unsigned char *pbuf, ebuf[MAX_EEPROM_SIZE];

	if (len == 0)
		return -1;

	timer_start();
	memset(ebuf, 0xff, sizeof(ebuf));
	pbuf = ebuf;

	if (offs || len < eepromsize) {
		if (ch341readEEPROM(pbuf, eepromsize, &eeprom_info) < 0) {
			fprintf(stderr, "Couldn't read [%d] bytes from [%s] EEPROM\n", eepromsize, eepromname); // Use stderr
			return -1;
		}
		memset(pbuf + offs, 0xff, len);
	}

	if(ch341writeEEPROM(pbuf, eepromsize, &eeprom_info) < 0) {
		fprintf(stderr, "Failed to erase [%d] bytes of [%s] EEPROM address 0x%08lu\n", (int)len, eepromname, offs); // Use stderr
		return -1;
	}

	printf("Erased [%d] bytes of [%s] EEPROM address 0x%08lu\n", (int)len, eepromname, offs);
	timer_end();

	return 0;
}

int i2c_eeprom_write(unsigned char *buf, unsigned long to, unsigned long len)
{
	unsigned char *pbuf, ebuf[MAX_EEPROM_SIZE];

	if (len == 0)
		return -1;

	timer_start();
	memset(ebuf, 0xff, sizeof(ebuf));
	pbuf = ebuf;

	if (to || len < eepromsize) {
		if (ch341readEEPROM(pbuf, eepromsize, &eeprom_info) < 0) {
			fprintf(stderr, "Couldn't read [%d] bytes from [%s] EEPROM\n", (int)len, eepromname); // Use stderr
			return -1;
		}
	}
	memcpy(pbuf + to, buf, len);

	if(ch341writeEEPROM(pbuf, eepromsize, &eeprom_info) < 0) {
		fprintf(stderr, "Failed to write [%d] bytes of [%s] EEPROM address 0x%08lu\n", (int)len, eepromname, to); // Use stderr
		return -1;
	}

	printf("Wrote [%d] bytes to [%s] EEPROM address 0x%08lu\n", (int)len, eepromname, to);
	timer_end();

	return (int)len;
}

long i2c_init(void)
{
	if (config_stream(CH341_I2C_STANDARD_SPEED) < 0)
		return -1;

	if (eepromsize <= 0) {
		fprintf(stderr, "I2C EEPROM Not Detected!\n"); // Use stderr
		return -1;
	}

	bsize = 1;

	printf("I2C EEPROM chip: %s, Size: %d bytes\n", eepromname, eepromsize);

	return (long)eepromsize;
}

void support_i2c_eeprom_list(void)
{
	int i;

	printf("I2C EEPROM Support List:\n");
	for ( i = 0; i < (sizeof(eepromlist)/sizeof(struct EEPROM)); i++)
	{
		if (!eepromlist[i].size)
			break;
		printf("%03d. %s\n", i + 1, eepromlist[i].name);
	}
}

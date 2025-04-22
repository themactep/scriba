/*
 * mw_eeprom.c
 * Copyright (C) 2021 McMCC <mcmcc@mail.ru>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "bitbang_microwire.h"
#include "ch341a_gpio.h"
#include "timer.h"

extern struct gpio_cmd bb_func;
extern char eepromname[12];
extern unsigned int bsize;

int mw_eeprom_read(unsigned char *buf, unsigned long from, unsigned long len)
{
	unsigned char *pbuf, ebuf[MAX_MW_EEPROM_SIZE];

	if (len == 0)
		return -1;

	timer_start();
	memset(ebuf, 0, sizeof(ebuf));
	pbuf = ebuf;

	if (Read_EEPROM_3wire(pbuf, mw_eepromsize) < 0) { // Add error check
		fprintf(stderr, "Failed to read from [%s] EEPROM\n", eepromname); // Use stderr
		return -1;
	}
	memcpy(buf, pbuf + from, len);

	printf("Read [%lu] bytes from [%s] EEPROM address 0x%08lu\n", len, eepromname, from);
	timer_end();

	return (int)len;
}

int mw_eeprom_erase(unsigned long offs, unsigned long len)
{
	unsigned char *pbuf, ebuf[MAX_MW_EEPROM_SIZE];

	if (len == 0)
		return -1;

	timer_start();
	memset(ebuf, 0xff, sizeof(ebuf));
	pbuf = ebuf;

	if (offs || len < mw_eepromsize) {
		Read_EEPROM_3wire(pbuf, mw_eepromsize);
		memset(pbuf + offs, 0xff, len);
	}

	Erase_EEPROM_3wire(mw_eepromsize);

	if (offs || len < mw_eepromsize) {
		if (Write_EEPROM_3wire(pbuf, mw_eepromsize) < 0) {
			fprintf(stderr, "Failed to erase [%lu] bytes of [%s] EEPROM address 0x%08lu\n", len, eepromname, offs); // Use stderr
			return -1;
		}
	}

	printf("Erased [%lu] bytes of [%s] EEPROM address 0x%08lu\n", len, eepromname, offs);
	timer_end();

	return 0;
}

int mw_eeprom_write(unsigned char *buf, unsigned long to, unsigned long len)
{
	unsigned char *pbuf, ebuf[MAX_MW_EEPROM_SIZE];

	if (len == 0)
		return -1;

	timer_start();
	memset(ebuf, 0xff, sizeof(ebuf));
	pbuf = ebuf;

	if (to || len < mw_eepromsize) {
		Read_EEPROM_3wire(pbuf, mw_eepromsize);
	}
	memcpy(pbuf + to, buf, len);

	Erase_EEPROM_3wire(mw_eepromsize);

	if (Write_EEPROM_3wire(pbuf, mw_eepromsize) < 0) {
		fprintf(stderr, "Failed to write [%lu] bytes of [%s] EEPROM address 0x%08lu\n", len, eepromname, to); // Use stderr
		return -1;
	}

	printf("Wrote [%lu] bytes to [%s] EEPROM address 0x%08lu\n", len, eepromname, to);
	timer_end();

	return (int)len;
}

/*
               25xx  93xx
 PIN 22 - D7 - MISO  DO
 PIN 21 - D6 -
 PIN 20 - D5 - MOSI  DI
 PIN 19 - D4 -
 PIN 18 - D3 - CLK   CLK
 PIN 17 - D2 -
 PIN 16 - D1 -
 PIN 15 - D0 - CS    CS
*/

static int mw_gpio_init(void)
{
	int ret = 0;

	CLK  = 1 << 3;
	DO   = 1 << 7;
	DI   = 1 << 5;
	CSEL = 1 << 0;

	bb_func.gpio_setdir  = ch341a_gpio_setdir;
	bb_func.gpio_setbits = ch341a_gpio_setbits;
	bb_func.gpio_getbits = ch341a_gpio_getbits;

	if(bb_func.gpio_setdir)
		ret = bb_func.gpio_setdir();
	else
		return -1;

	if(ret < 0)
		return -1;

	return 0;
}

/* Removed unused __itoa function
static char *__itoa(int a)
{
	static char tmpi[32];
	memset(tmpi, 0, sizeof(tmpi));
	snprintf(tmpi, sizeof(tmpi), "%d", a);
	return tmpi;
}
*/

long mw_init(void)
{
	if (mw_eepromsize <= 0) {
		fprintf(stderr, "Microwire EEPROM Not Detected!\n"); // Use stderr
		return -1;
	}

	if (mw_gpio_init() < 0)
		return -1;

	bsize = 1;

	// Print fix_addr_len directly or use snprintf if needed, removed __itoa call
	if (fix_addr_len) {
		printf("Microwire EEPROM chip: %s, Size: %d bytes, Org: %d bits, fix addr len: %d\n", eepromname, mw_eepromsize / (org ? 2 : 1),
			org ? 16 : 8, fix_addr_len);
	} else {
		printf("Microwire EEPROM chip: %s, Size: %d bytes, Org: %d bits, fix addr len: Auto\n", eepromname, mw_eepromsize / (org ? 2 : 1),
			org ? 16 : 8);
	}


	return (long)mw_eepromsize;
}

void support_mw_eeprom_list(void)
{
	int i;

	printf("Microwire EEPROM Support List:\n");
	for (i = 0; i < (sizeof(mw_eepromlist)/sizeof(struct MW_EEPROM)); i++)
	{
		if (!mw_eepromlist[i].size)
			break;
		printf("%03d. %s\n", i + 1, mw_eepromlist[i].name);
	}
}
/* vim: set ts=8: */

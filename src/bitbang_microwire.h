/*
 * bitbang_microwire.h
 * A simple bitbang protocol for Microwire 8-pin serial EEPROMs
 * (93XX devices). Support organization 8bit and 16bit(8bit emulation).
 * Copyright (C) 2005-2021 Mokrushin I.V. aka McMCC mcmcc@mail.ru
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef _BITBANG_MICROWIRE_H
#define _BITBANG_MICROWIRE_H

// extern unsigned char ORG; /* organization 0 = 8 bit and 1 = 16 bit */ // Obsolete/Commented out
extern unsigned char CLK;
extern unsigned char DO;
extern unsigned char DI;
extern unsigned char CSEL;

extern int org;
extern int mw_eepromsize;
extern int fix_addr_len;

struct MW_EEPROM {
	char *name;
	unsigned int size;
};

struct gpio_cmd {
	int (*gpio_setdir)(void);
	int (*gpio_setbits)(unsigned char bit);
	int (*gpio_getbits)(unsigned char *data);
};

void Erase_EEPROM_3wire(int size_eeprom);
int Read_EEPROM_3wire(unsigned char *buffer, int size_eeprom);
int Write_EEPROM_3wire(unsigned char *buffer, int size_eeprom);
int deviceSize_3wire(char *eepromname);

const static struct MW_EEPROM mw_eepromlist[] = {
	{ "93c06", 32 },
	{ "93c16", 64 },
	{ "93c46", 128 },
	{ "93c56", 256 },
	{ "93c66", 512 },
	{ "93c76", 1024 },
	{ "93c86", 2048 },
	{ "93c96", 4096 },
	{ 0, 0 }
};

#define MAX_MW_EEPROM_SIZE	4096

#endif /* _BITBANG_MICROWIRE_H */
/* vim: set ts=8: */

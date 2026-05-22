/*
 * flashcmd_api.c
 * Copyright (C) 2018-2021 McMCC <mcmcc@mail.ru>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "flashcmd_api.h"
 	#include <stdio.h>
 	#include <stdlib.h>
 	#include <string.h>

 	#define __EEPROM___ "or EEPROM"
 	extern int eepromsize;
 	extern int mw_eepromsize;
 	extern int seepromsize;

long flash_cmd_init(struct flash_cmd *cmd)
{
	long flen = -1;

	if ((eepromsize <= 0) && (mw_eepromsize <= 0) && (seepromsize <= 0))
	{
		if ((flen = snor_init()) > 0)
		{
			cmd->flash_erase = snor_erase;
			cmd->flash_write = snor_write;
			cmd->flash_read = snor_read;
		}
		else if ((flen = snand_init()) > 0)
		{
			cmd->flash_erase = snand_erase;
			cmd->flash_write = snand_write;
			cmd->flash_read = snand_read;
		}
	}
	else if ((eepromsize > 0) || (mw_eepromsize > 0) || (seepromsize > 0))
	{
		if ((eepromsize > 0) && (flen = i2c_init()) > 0)
		{
			cmd->flash_erase = i2c_eeprom_erase;
			cmd->flash_write = i2c_eeprom_write;
			cmd->flash_read = i2c_eeprom_read;
		}
		else if ((mw_eepromsize > 0) && (flen = mw_init()) > 0)
		{
			cmd->flash_erase = mw_eeprom_erase;
			cmd->flash_write = mw_eeprom_write;
			cmd->flash_read = mw_eeprom_read;
		}
		else if ((seepromsize > 0) && (flen = spi_eeprom_init()) > 0)
		{
			cmd->flash_erase = spi_eeprom_erase;
			cmd->flash_write = spi_eeprom_write;
			cmd->flash_read = spi_eeprom_read;
		}
	}
	else								     // If no flash/EEPROM was successfully initialized
		fprintf(stderr, "\nFlash" __EEPROM___ " not found!!!!\n\n"); // Use stderr for errors

	return flen;
 }

 int flashcmd_verify(const unsigned char *data, unsigned long addr, unsigned long len, unsigned long file_len)
 {
 	int ret;
 	unsigned char *verify_buf = NULL;

 	if (!data || !len)
 		return 0;

 	verify_buf = (unsigned char *)malloc(len);
 	if (!verify_buf)
 	{
 		fprintf(stderr, "Malloc failed for verify buffer: len=%ld.\n", len);
 		return 0;
 	}

 	ret = snand_read(verify_buf, addr, len);
 	if (ret < 0)
 	{
 		fprintf(stderr, "Verify Read Status: BAD(%d)\n", ret);
 		free(verify_buf);
 		return 0;
 	}

 	if (memcmp(verify_buf, data, len) != 0)
 	{
 		fprintf(stderr, "Verify Status: BAD - Data mismatch\n");
 		free(verify_buf);
 		return 0;
 	}

 	free(verify_buf);
 	return 1;
 }

 void support_flash_list(void)
 {
 	support_snand_list();
 	printf("\n");
 	support_snor_list();
 	printf("\n");
 	support_i2c_eeprom_list();
 	printf("\n");
 	support_mw_eeprom_list();
 	printf("\n");
 	support_spi_eeprom_list();
}

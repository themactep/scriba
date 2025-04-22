/*
 * flashcmd_api.h
 * Copyright (C) 2018-2021 McMCC <mcmcc@mail.ru>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef __FLASHCMD_API_H__
#define __FLASHCMD_API_H__

#include "nandcmd_api.h"
#include "snorcmd_api.h"
#ifdef EEPROM_SUPPORT
#include "i2c_eeprom_api.h"
#include "mw_eeprom_api.h"
#include "spi_eeprom_api.h"
#endif

struct flash_cmd {
	int (*flash_read)(unsigned char *buf, unsigned long from, unsigned long len);
	int (*flash_erase)(unsigned long offs, unsigned long len);
	int (*flash_write)(unsigned char *buf, unsigned long to, unsigned long len);
};

long flash_cmd_init(struct flash_cmd *cmd);
void support_flash_list(void);

#endif /* __FLASHCMD_API_H__ */
/* vim: set ts=8: */

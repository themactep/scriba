/*
 * mw_eeprom_api.h
 * Copyright (C) 2021 McMCC <mcmcc@mail.ru>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef __MW_EEPROM_API_H__
#define __MW_EEPROM_API_H__

int mw_eeprom_read(unsigned char *buf, unsigned long from, unsigned long len);
int mw_eeprom_erase(unsigned long offs, unsigned long len);
int mw_eeprom_write(unsigned char *buf, unsigned long to, unsigned long len);
long mw_init(void);
void support_mw_eeprom_list(void);

#endif /* __MW_EEPROM_API_H__ */
/* vim: set ts=8: */

/*
 * i2c_eeprom_api.h
 * Copyright (C) 2021 McMCC <mcmcc@mail.ru>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef __I2C_EEPROM_API_H__
#define __I2C_EEPROM_API_H__

int i2c_eeprom_read(unsigned char *buf, unsigned long from, unsigned long len);
int i2c_eeprom_erase(unsigned long offs, unsigned long len);
int i2c_eeprom_write(unsigned char *buf, unsigned long to, unsigned long len);
long i2c_init(void);
void support_i2c_eeprom_list(void);

#endif /* __I2C_EEPROM_API_H__ */

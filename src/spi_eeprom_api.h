/*
 * spi_eeprom_api.h
 * Copyright (C) 2022 McMCC <mcmcc@mail.ru>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef __SPI_EEPROM_API_H__
#define __SPI_EEPROM_API_H__

int spi_eeprom_read(unsigned char *buf, unsigned long from, unsigned long len);
int spi_eeprom_erase(unsigned long offs, unsigned long len);
int spi_eeprom_write(unsigned char *buf, unsigned long to, unsigned long len);
long spi_eeprom_init(void);
void support_spi_eeprom_list(void);

#endif /* __SPI_EEPROM_API_H__ */
/* vim: set ts=8: */

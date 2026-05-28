/*
 * ch341a_gpio.h
 * Copyright (C) 2021 McMCC <mcmcc@mail.ru>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef __CH341A_GPIO_H__
#define __CH341A_GPIO_H__

#include <stdint.h>

int ch341a_gpio_setdir(void);
int ch341a_gpio_setbits(uint8_t bits);
int ch341a_gpio_getbits(uint8_t *data);

#endif /* __CH341A_GPIO_H__ */

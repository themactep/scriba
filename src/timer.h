/*
 * timer.h
 * Copyright (C) 2021 McMCC <mcmcc@mail.ru>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef __TIMER_H__
#define __TIMER_H__

#include <stdint.h>

void timer_start(void);
void timer_end(void);
int timer_progress(void);
void timer_print_progress(const char *msg, uint32_t current, uint32_t total);

#endif /* __TIMER_H__ */

/*
 * snorcmd_api.h
 * Copyright (C) 2018-2021 McMCC <mcmcc@mail.ru>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef __SNORCMD_API_H__
#define __SNORCMD_API_H__

int snor_read(unsigned char *buf, unsigned long from, unsigned long len);
int snor_erase(unsigned long offs, unsigned long len);
int snor_write(unsigned char *buf, unsigned long to, unsigned long len);
long snor_init(void);
void support_snor_list(void);

#endif /* __SNORCMD_API_H__ */
/* vim: set ts=8: */

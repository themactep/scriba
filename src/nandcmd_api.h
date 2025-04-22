/*
 * nandcmd_api.h
 * Copyright (C) 2018-2021 McMCC <mcmcc@mail.ru>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef __NANDCMD_API_H__
#define __NANDCMD_API_H__

int snand_read(unsigned char *buf, unsigned long from, unsigned long len);
int snand_erase(unsigned long offs, unsigned long len);
int snand_write(unsigned char *buf, unsigned long to, unsigned long len);
long snand_init(void);
void support_snand_list(void);

extern int ECC_fcheck;
extern int ECC_ignore;
extern int OOB_size;
extern int Skip_BAD_page;
extern unsigned char _ondie_ecc_flag;

#endif /* __NANDCMD_API_H__ */
/* vim: set ts=8: */

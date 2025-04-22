/*
 * spi_nand_flash_defs.h
 * Definitions for SPI NAND Access interface.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef _SPI_NAND_FLASH_DEFS_H_
#define _SPI_NAND_FLASH_DEFS_H_

#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include "types.h"
#include "spi_controller.h"
#include "nandcmd_api.h"
#include "timer.h"

/* SPI NAND Command Set */
#define _SPI_NAND_OP_GET_FEATURE 0x0F		     /* Get Feature */
#define _SPI_NAND_OP_SET_FEATURE 0x1F		     /* Set Feature */
#define _SPI_NAND_OP_PAGE_READ 0x13		     /* Load page data into cache of SPI NAND chip */
#define _SPI_NAND_OP_READ_FROM_CACHE_SINGLE 0x03     /* Read data from cache of SPI NAND chip, single speed*/
#define _SPI_NAND_OP_READ_FROM_CACHE_DUAL 0x3B	     /* Read data from cache of SPI NAND chip, dual speed*/
#define _SPI_NAND_OP_READ_FROM_CACHE_QUAD 0x6B	     /* Read data from cache of SPI NAND chip, quad speed*/
#define _SPI_NAND_OP_WRITE_ENABLE 0x06		     /* Enable write data to  SPI NAND chip */
#define _SPI_NAND_OP_WRITE_DISABLE 0x04		     /* Resetting the Write Enable Latch (WEL) */
#define _SPI_NAND_OP_PROGRAM_LOAD_SINGLE 0x02	     /* Write data into cache of SPI NAND chip with cache reset, single speed */
#define _SPI_NAND_OP_PROGRAM_LOAD_QUAD 0x32	     /* Write data into cache of SPI NAND chip with cache reset, quad speed */
#define _SPI_NAND_OP_PROGRAM_LOAD_RAMDOM_SINGLE 0x84 /* Write data into cache of SPI NAND chip, single speed */
#define _SPI_NAND_OP_PROGRAM_LOAD_RAMDOM_QUAD 0x34   /* Write data into cache of SPI NAND chip, quad speed */

#define _SPI_NAND_OP_PROGRAM_EXECUTE 0x10 /* Write data from cache into SPI NAND chip */
#define _SPI_NAND_OP_READ_ID 0x9F	  /* Read Manufacture ID and Device ID */
#define _SPI_NAND_OP_BLOCK_ERASE 0xD8	  /* Erase Block */
#define _SPI_NAND_OP_RESET 0xFF		  /* Reset */
#define _SPI_NAND_OP_DIE_SELECT 0xC2	  /* Die Select */

/* SPI NAND register address of command set */
#define _SPI_NAND_ADDR_ECC 0x90		   /* Address of ECC Config */
#define _SPI_NAND_ADDR_PROTECTION 0xA0	   /* Address of protection */
#define _SPI_NAND_ADDR_FEATURE 0xB0	   /* Address of feature */
#define _SPI_NAND_ADDR_STATUS 0xC0	   /* Address of status */
#define _SPI_NAND_ADDR_FEATURE_4 0xD0	   /* Address of status 4 */
#define _SPI_NAND_ADDR_STATUS_5 0xE0	   /* Address of status 5 */
#define _SPI_NAND_ADDR_MANUFACTURE_ID 0x00 /* Address of Manufacture ID */
#define _SPI_NAND_ADDR_DEVICE_ID 0x01	   /* Address of Device ID */

/* SPI NAND value of register address of command set */
#define _SPI_NAND_VAL_DISABLE_PROTECTION 0x0 /* Value for disable write protection */
#define _SPI_NAND_VAL_ENABLE_PROTECTION 0x38 /* Value for enable write protection */
#define _SPI_NAND_VAL_OIP 0x1		     /* OIP = Operation In Progress */
#define _SPI_NAND_VAL_ERASE_FAIL 0x4	     /* E_FAIL = Erase Fail */
#define _SPI_NAND_VAL_PROGRAM_FAIL 0x8	     /* P_FAIL = Program Fail */
#define _SPI_NAND_VAL_ECC_ENABLE 0x10	     /* ECC Enable bit */
/* ECC Status bits (Status Register C0h) - Note: Interpretation varies by manufacturer! */
#define _SPI_NAND_VAL_ECC_STATUS_MASK_30 0x30	  /* Common mask for 2-bit ECC status */
#define _SPI_NAND_VAL_ECC_STATUS_MASK_70 0x70	  /* Common mask for 3-bit ECC status */
#define _SPI_NAND_VAL_ECC_STATUS_MASK_F0 0xF0	  /* Mask for 4-bit ECC status (e.g., XTX C-series) */
#define _SPI_NAND_VAL_ECC_STATUS_MASK_3C 0x3C	  /* Mask for ECC status (e.g., XTX A-series) */
#define _SPI_NAND_VAL_ECC_STATUS_SHIFT_4 4	  /* Common shift for ECC status */
#define _SPI_NAND_VAL_ECC_STATUS_SHIFT_2 2	  /* Shift for ECC status (e.g., XTX A-series) */
#define _SPI_NAND_VAL_ECC_UNCORRECTABLE_2BIT 0x2  /* Common value indicating uncorrectable error (for 0x30 mask) */
#define _SPI_NAND_VAL_ECC_UNCORRECTABLE_3BIT 0x7  /* Common value indicating uncorrectable error (for 0x70 mask) */
#define _SPI_NAND_VAL_ECC_UNCORRECTABLE_XTX_A 0x8 /* Value indicating uncorrectable error (for 0x3C mask) */
#define _SPI_NAND_VAL_ECC_UNCORRECTABLE_XTX_C 0xF /* Value indicating uncorrectable error (for 0xF0 mask) */

/* Die Select Bits (Specific to certain manufacturers/methods) */
#define _SPI_NAND_VAL_DIE_SELECT_BIT_MICRON 0x40 /* Bit used for die select in Micron status reg 4 method */

/* SPI NAND Size Define */
#define _SPI_NAND_PAGE_SIZE_512 0x0200
#define _SPI_NAND_PAGE_SIZE_2KBYTE 0x0800
#define _SPI_NAND_PAGE_SIZE_4KBYTE 0x1000
#define _SPI_NAND_OOB_SIZE_64BYTE 0x40
#define _SPI_NAND_OOB_SIZE_120BYTE 0x78
#define _SPI_NAND_OOB_SIZE_128BYTE 0x80
#define _SPI_NAND_OOB_SIZE_256BYTE 0x100
#define _SPI_NAND_BLOCK_SIZE_128KBYTE 0x20000
#define _SPI_NAND_BLOCK_SIZE_256KBYTE 0x40000
#define _SPI_NAND_CHIP_SIZE_512MBIT 0x04000000
#define _SPI_NAND_CHIP_SIZE_1GBIT 0x08000000
#define _SPI_NAND_CHIP_SIZE_2GBIT 0x10000000
#define _SPI_NAND_CHIP_SIZE_4GBIT 0x20000000

/* SPI NAND Manufacturers ID */
#define _SPI_NAND_MANUFACTURER_ID_GIGADEVICE 0xC8
#define _SPI_NAND_MANUFACTURER_ID_WINBOND 0xEF
#define _SPI_NAND_MANUFACTURER_ID_ESMT 0xC8
#define _SPI_NAND_MANUFACTURER_ID_MXIC 0xC2
#define _SPI_NAND_MANUFACTURER_ID_ZENTEL 0xC8
#define _SPI_NAND_MANUFACTURER_ID_ETRON 0xD5
#define _SPI_NAND_MANUFACTURER_ID_TOSHIBA 0x98
#define _SPI_NAND_MANUFACTURER_ID_MICRON 0x2C
#define _SPI_NAND_MANUFACTURER_ID_HEYANG 0xC9
#define _SPI_NAND_MANUFACTURER_ID_HEYANG_2 0x01
#define _SPI_NAND_MANUFACTURER_ID_PN 0xA1
#define _SPI_NAND_MANUFACTURER_ID_ATO 0x9B
#define _SPI_NAND_MANUFACTURER_ID_ATO_2 0xAD
#define _SPI_NAND_MANUFACTURER_ID_FM 0xA1
#define _SPI_NAND_MANUFACTURER_ID_XTX 0x0B
#define _SPI_NAND_MANUFACTURER_ID_MIRA 0xC8
#define _SPI_NAND_MANUFACTURER_ID_BIWIN 0xBC
#define _SPI_NAND_MANUFACTURER_ID_FORESEE 0xCD
#define _SPI_NAND_MANUFACTURER_ID_DS 0xE5
#define _SPI_NAND_MANUFACTURER_ID_FISON 0x6B
#define _SPI_NAND_MANUFACTURER_ID_TYM 0x19
#define _SPI_NAND_MANUFACTURER_ID_XINCUN 0x9C

/* SPI NAND Device ID */
#define _SPI_NAND_DEVICE_ID_GD5F1GQ4UAYIG 0xF1
#define _SPI_NAND_DEVICE_ID_GD5F2GQ4UAYIG 0xF2
#define _SPI_NAND_DEVICE_ID_GD5F1GQ4UBYIG 0xD1
#define _SPI_NAND_DEVICE_ID_GD5F1GQ4REYIG 0xC1
#define _SPI_NAND_DEVICE_ID_GD5F1GQ4UCYIG 0xB1
#define _SPI_NAND_DEVICE_ID_GD5F2GQ4UBYIG 0xD2
#define _SPI_NAND_DEVICE_ID_GD5F1GQ4UEYIS 0xD3
#define _SPI_NAND_DEVICE_ID_GD5F2GQ4UE9IS 0xD5
#define _SPI_NAND_DEVICE_ID_GD5F2GQ4UCYIG 0xB2
#define _SPI_NAND_DEVICE_ID_GD5F4GQ4UBYIG 0xD4
#define _SPI_NAND_DEVICE_ID_GD5F4GQ4UCYIG 0xB4
#define _SPI_NAND_DEVICE_ID_GD5F1GQ5UEYIG 0x51
#define _SPI_NAND_DEVICE_ID_GD5F1GQ5REYIG 0x41
#define _SPI_NAND_DEVICE_ID_GD5F2GQ5UEYIG 0x52
#define _SPI_NAND_DEVICE_ID_GD5F2GQ5REYIG 0x42
#define _SPI_NAND_DEVICE_ID_GD5F1GM7UEYIG 0x91
#define _SPI_NAND_DEVICE_ID_GD5F1GM7REYIG 0x81
#define _SPI_NAND_DEVICE_ID_GD5F2GM7UEYIG 0x92
#define _SPI_NAND_DEVICE_ID_GD5F2GM7REYIG 0x82
#define _SPI_NAND_DEVICE_ID_F50D1G41LB 0x11
#define _SPI_NAND_DEVICE_ID_F50L512M41A 0x20
#define _SPI_NAND_DEVICE_ID_F50L1G41A0 0x21
#define _SPI_NAND_DEVICE_ID_F50L1G41LB 0x01
#define _SPI_NAND_DEVICE_ID_F50L2G41LB 0x0A
#define _SPI_NAND_DEVICE_ID_1_W25N01KV 0xAE
#define _SPI_NAND_DEVICE_ID_2_W25N01KV 0x21
#define _SPI_NAND_DEVICE_ID_1_W25N01GV 0xAA
#define _SPI_NAND_DEVICE_ID_2_W25N01GV 0x21
#define _SPI_NAND_DEVICE_ID_1_W25N01GW 0xBA
#define _SPI_NAND_DEVICE_ID_2_W25N01GW 0x21
#define _SPI_NAND_DEVICE_ID_1_W25N02KV 0xAA
#define _SPI_NAND_DEVICE_ID_2_W25N02KV 0x22
#define _SPI_NAND_DEVICE_ID_1_W25N04KV 0xAA
#define _SPI_NAND_DEVICE_ID_2_W25N04KV 0x23
#define _SPI_NAND_DEVICE_ID_1_W25N04KW 0xBA
#define _SPI_NAND_DEVICE_ID_2_W25N04KW 0x23
#define _SPI_NAND_DEVICE_ID_1_W25M02GV 0xAB
#define _SPI_NAND_DEVICE_ID_2_W25M02GV 0x21
#define _SPI_NAND_DEVICE_ID_MX35LF1GE4AB 0x12
#define _SPI_NAND_DEVICE_ID_MX35LF2GE4AB 0x22
#define _SPI_NAND_DEVICE_ID_MX35LF2G14AC 0x20
#define _SPI_NAND_DEVICE_ID_1_MX35LF2GE4AD 0x26
#define _SPI_NAND_DEVICE_ID_2_MX35LF2GE4AD 0x03
#define _SPI_NAND_DEVICE_ID_1_MX35LF1G24AD 0x14
#define _SPI_NAND_DEVICE_ID_2_MX35LF1G24AD 0x03
#define _SPI_NAND_DEVICE_ID_1_MX35LF2G24AD 0x24
#define _SPI_NAND_DEVICE_ID_2_MX35LF2G24AD 0x03
#define _SPI_NAND_DEVICE_ID_1_MX35LF4G24AD 0x35
#define _SPI_NAND_DEVICE_ID_2_MX35LF4G24AD 0x03
#define _SPI_NAND_DEVICE_ID_1_MX35LF2G24ADZ4I8 0x64
#define _SPI_NAND_DEVICE_ID_2_MX35LF2G24ADZ4I8 0x03
#define _SPI_NAND_DEVICE_ID_1_MX35LF4G24ADZ4I8 0x75
#define _SPI_NAND_DEVICE_ID_2_MX35LF4G24ADZ4I8 0x03
#define _SPI_NAND_DEVICE_ID_MX35UF1G14AC 0x90
#define _SPI_NAND_DEVICE_ID_MX35UF2G14AC 0xA0
#define _SPI_NAND_DEVICE_ID_1_MX35UF1GE4AD 0x96
#define _SPI_NAND_DEVICE_ID_2_MX35UF1GE4AD 0x03
#define _SPI_NAND_DEVICE_ID_1_MX35UF2GE4AD 0xA6
#define _SPI_NAND_DEVICE_ID_2_MX35UF2GE4AD 0x03
#define _SPI_NAND_DEVICE_ID_1_MX35UF4GE4AD 0xB7
#define _SPI_NAND_DEVICE_ID_2_MX35UF4GE4AD 0x03
#define _SPI_NAND_DEVICE_ID_A5U12A21ASC 0x20
#define _SPI_NAND_DEVICE_ID_A5U1GA21BWS 0x21
#define _SPI_NAND_DEVICE_ID_EM73C044SNA 0x19
#define _SPI_NAND_DEVICE_ID_EM73C044SNB 0x11
#define _SPI_NAND_DEVICE_ID_EM73C044SND 0x1D
#define _SPI_NAND_DEVICE_ID_EM73C044SNF 0x09
#define _SPI_NAND_DEVICE_ID_EM73C044VCA 0x18
#define _SPI_NAND_DEVICE_ID_EM73C044VCD 0x1C
#define _SPI_NAND_DEVICE_ID_EM73D044SNA 0x12
#define _SPI_NAND_DEVICE_ID_EM73D044SNC 0x0A
#define _SPI_NAND_DEVICE_ID_EM73D044SND 0x1E
#define _SPI_NAND_DEVICE_ID_EM73D044SNF 0x10
#define _SPI_NAND_DEVICE_ID_EM73D044VCA 0x13
#define _SPI_NAND_DEVICE_ID_EM73D044VCB 0x14
#define _SPI_NAND_DEVICE_ID_EM73D044VCD 0x17
#define _SPI_NAND_DEVICE_ID_EM73D044VCG 0x1F
#define _SPI_NAND_DEVICE_ID_EM73D044VCH 0x1B
#define _SPI_NAND_DEVICE_ID_EM73E044SNA 0x03
#define _SPI_NAND_DEVICE_ID_TC58CVG0S3H 0xC2
#define _SPI_NAND_DEVICE_ID_TC58CVG1S3H 0xCB
#define _SPI_NAND_DEVICE_ID_TC58CVG2S0H 0xCD
#define _SPI_NAND_DEVICE_ID_TC58CVG2S0HRAIJ 0xED
#define _SPI_NAND_DEVICE_ID_MT29F1G01AA 0x12
#define _SPI_NAND_DEVICE_ID_MT29F2G01AA 0x22
#define _SPI_NAND_DEVICE_ID_MT29F4G01AA 0x32
#define _SPI_NAND_DEVICE_ID_MT29F1G01AB 0x14
#define _SPI_NAND_DEVICE_ID_MT29F2G01ABA 0x24
#define _SPI_NAND_DEVICE_ID_MT29F2G01ABB 0x25
#define _SPI_NAND_DEVICE_ID_MT29F4G01ABA 0x34
#define _SPI_NAND_DEVICE_ID_MT29F4G01ABB 0x35
#define _SPI_NAND_DEVICE_ID_MT29F4G01AD 0x36
#define _SPI_NAND_DEVICE_ID_HYF1GQ4UAACAE 0x51
#define _SPI_NAND_DEVICE_ID_HYF2GQ4UAACAE 0x52
#define _SPI_NAND_DEVICE_ID_HYF2GQ4UHCCAE 0x5A
#define _SPI_NAND_DEVICE_ID_HYF1GQ4UDACAE 0x21
#define _SPI_NAND_DEVICE_ID_HYF2GQ4UDACAE 0x22
#define _SPI_NAND_DEVICE_ID_HYF1GQ4UTACAE 0x15
#define _SPI_NAND_DEVICE_ID_HYF2GQ4UTACAE 0x25
#define _SPI_NAND_DEVICE_ID_PN26G01AWSIUG 0xE1
#define _SPI_NAND_DEVICE_ID_PN26G02AWSIUG 0xE2
#define _SPI_NAND_DEVICE_ID_PN26Q01AWSIUG 0xC1
#define _SPI_NAND_DEVICE_ID_ATO25D1GA 0x12
#define _SPI_NAND_DEVICE_ID_ATO25D2GA 0xF1
#define _SPI_NAND_DEVICE_ID_ATO25D2GB 0xDA
#define _SPI_NAND_DEVICE_ID_FM25S01 0xA1
#define _SPI_NAND_DEVICE_ID_FM25S01A 0xE4
#define _SPI_NAND_DEVICE_ID_FM25S02A 0xE5
#define _SPI_NAND_DEVICE_ID_FM25G01B 0xD1
#define _SPI_NAND_DEVICE_ID_FM25G02B 0xD2
#define _SPI_NAND_DEVICE_ID_FM25G02 0xF2
#define _SPI_NAND_DEVICE_ID_FM25G02C 0x92
#define _SPI_NAND_DEVICE_ID_XT26G02B 0xF2
#define _SPI_NAND_DEVICE_ID_XT26G01C 0x11
#define _SPI_NAND_DEVICE_ID_XT26G02C 0x12
#define _SPI_NAND_DEVICE_ID_XT26G01A 0xE1
#define _SPI_NAND_DEVICE_ID_XT26G02A 0xE2
#define _SPI_NAND_DEVICE_ID_PSU1GS20BN 0x21
#define _SPI_NAND_DEVICE_ID_BWJX08U 0xB1
#define _SPI_NAND_DEVICE_ID_BWET08U 0xB2
#define _SPI_NAND_DEVICE_ID_F35UQA512M 0x60
#define _SPI_NAND_DEVICE_ID_F35UQA001G 0x61
#define _SPI_NAND_DEVICE_ID_F35UQA002G 0x62
#define _SPI_NAND_DEVICE_ID_F35SQA512M 0x70
#define _SPI_NAND_DEVICE_ID_F35SQA001G 0x71
#define _SPI_NAND_DEVICE_ID_F35SQA002G 0x72
#define _SPI_NAND_DEVICE_ID_FS35ND01GD1F1 0xA1
#define _SPI_NAND_DEVICE_ID_FS35ND01GS1F1 0xB1
#define _SPI_NAND_DEVICE_ID_FS35ND02GS2F1 0xA2
#define _SPI_NAND_DEVICE_ID_FS35ND02GD1F1 0xB2
#define _SPI_NAND_DEVICE_ID_FS35ND01GS1Y2 0xEA
#define _SPI_NAND_DEVICE_ID_FS35ND02GS3Y2 0xEB
#define _SPI_NAND_DEVICE_ID_FS35ND04GS2Y2 0xEC
#define _SPI_NAND_DEVICE_ID_DS35Q1GA 0x71
#define _SPI_NAND_DEVICE_ID_DS35M1GA 0x21
#define _SPI_NAND_DEVICE_ID_DS35Q2GA 0x72
#define _SPI_NAND_DEVICE_ID_DS35M2GA 0x22
#define _SPI_NAND_DEVICE_ID_DS35Q2GB 0xF2
#define _SPI_NAND_DEVICE_ID_DS35M2GB 0xA2
#define _SPI_NAND_DEVICE_ID_CS11G0T0A0AA 0x00
#define _SPI_NAND_DEVICE_ID_CS11G1T0A0AA 0x01
#define _SPI_NAND_DEVICE_ID_CS11G0G0A0AA 0x10
#define _SPI_NAND_DEVICE_ID_TYM25D2GA01 0x01
#define _SPI_NAND_DEVICE_ID_TYM25D2GA02 0x02
#define _SPI_NAND_DEVICE_ID_TYM25D1GA03 0x03
#define _SPI_NAND_DEVICE_ID_XCSP1AAWHNT 0x01

/* Others Define */
#define _SPI_NAND_LEN_ONE_BYTE (1)
#define _SPI_NAND_LEN_TWO_BYTE (2)
#define _SPI_NAND_LEN_THREE_BYTE (3)
#define _SPI_NAND_BLOCK_ROW_ADDRESS_OFFSET (6)

#define _SPI_NAND_OOB_SIZE 256
#define _SPI_NAND_PAGE_SIZE (4096 + _SPI_NAND_OOB_SIZE)
#define _SPI_NAND_CACHE_SIZE (_SPI_NAND_PAGE_SIZE + _SPI_NAND_OOB_SIZE)

/* Timeout and other constants */
#define SPI_NAND_FLASH_TIMEOUT 1000000

#define LINUX_USE_OOB_START_OFFSET 4
#define MAX_LINUX_USE_OOB_SIZE 26

#define EMPTY_DATA (0)
#define NONE_EMPTY_DATA (1)
#define EMPTY_OOB (0)
#define NONE_EMPTY_OOB (1)

#define _SPI_NAND_BLOCK_ALIGNED_CHECK(__addr__, __block_size__) ((__addr__) & (__block_size__ - 1))
#define _SPI_NAND_GET_DEVICE_INFO_PTR &(_current_flash_info_t)

/* Porting Replacement */
#define _SPI_NAND_SEMAPHORE_LOCK()   /* Disable interrupt */
#define _SPI_NAND_SEMAPHORE_UNLOCK() /* Enable interrupt  */
#define _SPI_NAND_PRINTF printf	     /* Always print information */
#if !defined(SPI_NAND_FLASH_DEBUG)
#define _SPI_NAND_DEBUG_PRINTF(level, fmt, args...)
#define _SPI_NAND_DEBUG_PRINTF_ARRAY(level, array, len)
#else
#define _SPI_NAND_DEBUG_PRINTF(level, fmt, args...) printf(fmt, ##args)
void _SPI_NAND_DEBUG_PRINTF_ARRAY(int level, u8 *array, int len)
{
	for (int i = 0; i < len; i++)
	{
		_SPI_NAND_DEBUG_PRINTF(level, "  %02X", array[i]);
		if ((i % 16) == 15)
			_SPI_NAND_DEBUG_PRINTF(level, "\n");
	}
	if ((len % 16) != 0)
		_SPI_NAND_DEBUG_PRINTF(level, "\n");
}
#endif
#define _SPI_NAND_ENABLE_MANUAL_MODE SPI_CONTROLLER_Enable_Manual_Mode
#define _SPI_NAND_WRITE_ONE_BYTE SPI_CONTROLLER_Write_One_Byte
// Remove speed argument from macro calls
#define _SPI_NAND_WRITE_NBYTE(ptr, len, speed) SPI_CONTROLLER_Write_NByte(ptr, len)
#define _SPI_NAND_READ_NBYTE(ptr, len, speed) SPI_CONTROLLER_Read_NByte(ptr, len)
#define _SPI_NAND_READ_CHIP_SELECT_HIGH SPI_CONTROLLER_Chip_Select_High
#define _SPI_NAND_READ_CHIP_SELECT_LOW SPI_CONTROLLER_Chip_Select_Low

/* spi_nand_flash_protocol.c function prototypes */
SPI_NAND_FLASH_RTN_T spi_nand_protocol_set_feature(u8 addr, u8 data);
SPI_NAND_FLASH_RTN_T spi_nand_protocol_get_feature(u8 addr, u8 *ptr_rtn_data);
SPI_NAND_FLASH_RTN_T spi_nand_protocol_set_status_reg_1(u8 protection);
SPI_NAND_FLASH_RTN_T spi_nand_protocol_get_status_reg_1(u8 *ptr_rtn_protection);
SPI_NAND_FLASH_RTN_T spi_nand_protocol_set_status_reg_2(u8 feature);
SPI_NAND_FLASH_RTN_T spi_nand_protocol_get_status_reg_2(u8 *ptr_rtn_feature);
SPI_NAND_FLASH_RTN_T spi_nand_protocol_get_status_reg_3(u8 *ptr_rtn_status);
SPI_NAND_FLASH_RTN_T spi_nand_protocol_set_status_reg_4(u8 feature);
SPI_NAND_FLASH_RTN_T spi_nand_protocol_get_status_reg_4(u8 *ptr_rtn_status);
SPI_NAND_FLASH_RTN_T spi_nand_protocol_get_status_reg_5(u8 *ptr_rtn_status);
SPI_NAND_FLASH_RTN_T spi_nand_protocol_write_enable(void);
SPI_NAND_FLASH_RTN_T spi_nand_protocol_write_disable(void);
SPI_NAND_FLASH_RTN_T spi_nand_protocol_block_erase(u32 block_idx);
SPI_NAND_FLASH_RTN_T spi_nand_protocol_read_id(struct SPI_NAND_FLASH_INFO_T *ptr_rtn_flash_id);
SPI_NAND_FLASH_RTN_T spi_nand_protocol_read_id_2(struct SPI_NAND_FLASH_INFO_T *ptr_rtn_flash_id);
SPI_NAND_FLASH_RTN_T spi_nand_protocol_read_id_3(struct SPI_NAND_FLASH_INFO_T *ptr_rtn_flash_id);
SPI_NAND_FLASH_RTN_T spi_nand_protocol_page_read(u32 page_number);
SPI_NAND_FLASH_RTN_T spi_nand_protocol_read_from_cache(u32 data_offset, u32 len, u8 *ptr_rtn_buf, u32 read_mode, SPI_NAND_FLASH_READ_DUMMY_BYTE_T dummy_mode);
SPI_NAND_FLASH_RTN_T spi_nand_protocol_program_load(u32 addr, u8 *ptr_data, u32 len, u32 write_mode);
SPI_NAND_FLASH_RTN_T spi_nand_protocol_program_execute(u32 addr);
SPI_NAND_FLASH_RTN_T spi_nand_protocol_die_select_1(u8 die_id);
SPI_NAND_FLASH_RTN_T spi_nand_protocol_die_select_2(u8 die_id);

#endif /* _SPI_NAND_FLASH_DEFS_H_ */

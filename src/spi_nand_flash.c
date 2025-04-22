/*
 * spi_nand_flash.c
 * SPI NAND Access interface.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include "types.h"
#include "spi_nand_flash.h"
#include "spi_controller.h"
#include "nandcmd_api.h"
#include "timer.h"
#include "spi_nand_flash_defs.h"

int ECC_fcheck = 1;
int ECC_ignore = 0;
int OOB_size = 0;
int Skip_BAD_page = 0;

unsigned char _plane_select_bit = 0;
static unsigned char _die_id = 0;
int en_oob_write = 0;
int en_oob_erase = 0;

unsigned char _ondie_ecc_flag = 1;    /* Ondie ECC : [ToDo :  Init this flag base on diffrent chip ?] */

#define IMAGE_OOB_SIZE	64	/* fix 64 oob buffer size padding after page buffer, no hw ecc info */
#define PAGE_OOB_SIZE	64	/* 64 bytes for 2K page, 128 bytes for 4k page */

#define BLOCK_SIZE 4096 /* 4K pages */ (_current_flash_info_t.erase_size)

static unsigned long bmt_oob_size = PAGE_OOB_SIZE;
static u32 erase_oob_size = 0;
static u32 ecc_size = 0;
u32 bsize = 0;

static u32 _current_page_num = 0xFFFFFFFF;
static u8 _current_cache_page[_SPI_NAND_CACHE_SIZE];
static u8 _current_cache_page_data[_SPI_NAND_PAGE_SIZE];
static u8 _current_cache_page_oob[_SPI_NAND_OOB_SIZE];
static u8 _current_cache_page_oob_mapping[_SPI_NAND_OOB_SIZE];

struct SPI_NAND_FLASH_INFO_T _current_flash_info_t;	/* Store the current flash information */

/* External declaration for the flash tables defined in spi_nand_flash_tables.c */
extern const struct SPI_NAND_FLASH_INFO_T spi_nand_flash_tables[];
extern size_t get_spi_nand_flash_table_size(void);

static void spi_nand_select_die ( u32 page_number )
{
	struct SPI_NAND_FLASH_INFO_T *ptr_dev_info_t;
	u8 die_id = 0;

	ptr_dev_info_t = _SPI_NAND_GET_DEVICE_INFO_PTR;

	if( ((ptr_dev_info_t->feature) & SPI_NAND_FLASH_DIE_SELECT_1_HAVE) ) {
		/* single die = 1024blocks * 64pages */
		die_id = ((page_number >> 16) & 0xff);

		if (_die_id != die_id)
		{
			_die_id = die_id;
			spi_nand_protocol_die_select_1(die_id);

			_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_2, "spi_nand_protocol_die_select_1: die_id=0x%x\n", die_id);
		}
	} else if( ((ptr_dev_info_t->feature) & SPI_NAND_FLASH_DIE_SELECT_2_HAVE) ) {
		/* single die = 2plans * 1024blocks * 64pages */
		die_id = ((page_number >> 17) & 0xff);

		if (_die_id != die_id)
		{
			_die_id = die_id;
			spi_nand_protocol_die_select_2(die_id);

			_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_2, "spi_nand_protocol_die_select_2: die_id=0x%x\n", die_id);
		}
	}
}

static SPI_NAND_FLASH_RTN_T ecc_fail_check( u32 page_number )
{
	u8 status;
	struct SPI_NAND_FLASH_INFO_T *ptr_dev_info_t;
	SPI_NAND_FLASH_RTN_T rtn_status = SPI_NAND_FLASH_RTN_NO_ERROR;
	ptr_dev_info_t = _SPI_NAND_GET_DEVICE_INFO_PTR;

	spi_nand_protocol_get_status_reg_3( &status);

	_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "ecc_fail_check: status = 0x%x\n", status);

	if ((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_GIGADEVICE) &&
		((ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F1GQ4UAYIG) ||
		 (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F2GQ4UAYIG) ||
		 (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F1GQ4UBYIG) ||
		 (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F1GQ4REYIG) ||
		 (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F1GQ4UCYIG) ||
		 (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F1GQ4UEYIS) ||
		 (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F1GQ5UEYIG) ||
		 (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F1GQ5REYIG) ||
		 (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F2GQ4UBYIG) ||
		 (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F2GQ4UE9IS) ||
		 (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F2GQ4UCYIG) ||
		 (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F4GQ4UBYIG) ||
		 (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F4GQ4UCYIG) ||
		 (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F2GQ5UEYIG) ||
		 (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F2GQ5REYIG) ||
		 (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F1GM7UEYIG) ||
		 (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F1GM7REYIG) ||
		 (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F2GM7UEYIG) ||
			(ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F2GM7REYIG)))
	{
		// GigaDevice Type 1 ECC Check (Uses 0x30 mask)
		if ((ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F1GQ4UAYIG) ||
			(ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F2GQ4UAYIG) ||
			(ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F1GQ4UBYIG) ||
			(ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F1GQ4REYIG) ||
			(ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F1GQ4UEYIS) ||
			(ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F1GQ5UEYIG) ||
			(ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F1GQ5REYIG) ||
			(ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F2GQ4UBYIG) ||
			(ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F2GQ4UE9IS) ||
			(ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F4GQ4UBYIG) ||
			(ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F2GQ5UEYIG) ||
			(ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F2GQ5REYIG) ||
			(ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F1GM7UEYIG) ||
			(ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F1GM7REYIG) ||
			(ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F2GM7UEYIG) ||
			(ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F2GM7REYIG))
		{
			if (((status & _SPI_NAND_VAL_ECC_STATUS_MASK_30) >> _SPI_NAND_VAL_ECC_STATUS_SHIFT_4) == _SPI_NAND_VAL_ECC_UNCORRECTABLE_2BIT )
			{
				rtn_status = SPI_NAND_FLASH_RTN_DETECTED_BAD_BLOCK;
			}
		}
		// GigaDevice Type 2 ECC Check (Uses 0x70 mask)
		else if ((ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F1GQ4UCYIG) ||
			(ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F2GQ4UCYIG) ||
			(ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F4GQ4UCYIG))
		{
			if (((status & _SPI_NAND_VAL_ECC_STATUS_MASK_70) >> _SPI_NAND_VAL_ECC_STATUS_SHIFT_4) == _SPI_NAND_VAL_ECC_UNCORRECTABLE_3BIT)
			{
				rtn_status = SPI_NAND_FLASH_RTN_DETECTED_BAD_BLOCK;
			}
		}
	}
	else if(ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_MXIC)
	{
		if(((status & _SPI_NAND_VAL_ECC_STATUS_MASK_30) >> _SPI_NAND_VAL_ECC_STATUS_SHIFT_4) == _SPI_NAND_VAL_ECC_UNCORRECTABLE_2BIT )
		{
			rtn_status = SPI_NAND_FLASH_RTN_DETECTED_BAD_BLOCK;
		}
	}
	else if(ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_WINBOND)
	{
		if(((status & _SPI_NAND_VAL_ECC_STATUS_MASK_30) >> _SPI_NAND_VAL_ECC_STATUS_SHIFT_4) == _SPI_NAND_VAL_ECC_UNCORRECTABLE_2BIT)
		{
			rtn_status = SPI_NAND_FLASH_RTN_DETECTED_BAD_BLOCK;
		}
	}
	else if((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_ESMT) &&
		((ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_F50D1G41LB) ||
		 (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_F50L512M41A) ||
		 (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_F50L1G41A0) ||
		 (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_F50L1G41LB) ||
		 (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_F50L2G41LB)))
	{
		if(((status & _SPI_NAND_VAL_ECC_STATUS_MASK_30) >> _SPI_NAND_VAL_ECC_STATUS_SHIFT_4) == _SPI_NAND_VAL_ECC_UNCORRECTABLE_2BIT)
		{
			rtn_status = SPI_NAND_FLASH_RTN_DETECTED_BAD_BLOCK;
		}
	}
	else if((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_ZENTEL) &&
		((ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_A5U12A21ASC) ||
		 (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_A5U1GA21BWS)))
	{
		if(((status & _SPI_NAND_VAL_ECC_STATUS_MASK_30) >> _SPI_NAND_VAL_ECC_STATUS_SHIFT_4) == _SPI_NAND_VAL_ECC_UNCORRECTABLE_2BIT )
		{
			rtn_status = SPI_NAND_FLASH_RTN_DETECTED_BAD_BLOCK;
		}
	}
	else if(ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_ETRON)
	{
		if(((status & _SPI_NAND_VAL_ECC_STATUS_MASK_30) >> _SPI_NAND_VAL_ECC_STATUS_SHIFT_4) == _SPI_NAND_VAL_ECC_UNCORRECTABLE_2BIT)
		{
			rtn_status = SPI_NAND_FLASH_RTN_DETECTED_BAD_BLOCK;
		}
	}
	else if(ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_TOSHIBA)
	{
		if(((status & _SPI_NAND_VAL_ECC_STATUS_MASK_30) >> _SPI_NAND_VAL_ECC_STATUS_SHIFT_4) == _SPI_NAND_VAL_ECC_UNCORRECTABLE_2BIT)
		{
			rtn_status = SPI_NAND_FLASH_RTN_DETECTED_BAD_BLOCK;
		}
	}
	else if(ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_MICRON)
	{
		// Micron uses 0x70 mask, but value 0x2 indicates uncorrectable
		if(((status & _SPI_NAND_VAL_ECC_STATUS_MASK_70) >> _SPI_NAND_VAL_ECC_STATUS_SHIFT_4) == 0x2)
		{
			rtn_status = SPI_NAND_FLASH_RTN_DETECTED_BAD_BLOCK;
		}
	}
	else if((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_HEYANG) ||
		(ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_HEYANG_2))
	{
		if(((status & _SPI_NAND_VAL_ECC_STATUS_MASK_30) >> _SPI_NAND_VAL_ECC_STATUS_SHIFT_4) == _SPI_NAND_VAL_ECC_UNCORRECTABLE_2BIT)
		{
			rtn_status = SPI_NAND_FLASH_RTN_DETECTED_BAD_BLOCK;
		}
	}
	else if(((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_PN) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_PN26G01AWSIUG)) ||
		((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_PN) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_PN26G02AWSIUG)) ||
		((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_PN) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_PN26Q01AWSIUG)))
	{
		if(((status & _SPI_NAND_VAL_ECC_STATUS_MASK_30) >> _SPI_NAND_VAL_ECC_STATUS_SHIFT_4) == _SPI_NAND_VAL_ECC_UNCORRECTABLE_2BIT)
		{
			rtn_status = SPI_NAND_FLASH_RTN_DETECTED_BAD_BLOCK;
		}
	}
	else if(((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_ATO) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_ATO25D2GA)) ||
		((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_ATO_2) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_ATO25D2GB)))
	{
		if(((status & _SPI_NAND_VAL_ECC_STATUS_MASK_30) >> _SPI_NAND_VAL_ECC_STATUS_SHIFT_4) == _SPI_NAND_VAL_ECC_UNCORRECTABLE_2BIT)
		{
			rtn_status = SPI_NAND_FLASH_RTN_DETECTED_BAD_BLOCK;
		}
	}
	else if(((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FM) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_FM25S01)) ||
		((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FM) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_FM25S01A)) ||
		((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FM) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_FM25S02A)))
	{
		if(((status & _SPI_NAND_VAL_ECC_STATUS_MASK_30) >> _SPI_NAND_VAL_ECC_STATUS_SHIFT_4) == _SPI_NAND_VAL_ECC_UNCORRECTABLE_2BIT)
		{
			rtn_status = SPI_NAND_FLASH_RTN_DETECTED_BAD_BLOCK;
		}
	}
	else if(((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FM) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_FM25G01B)) ||
		((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FM) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_FM25G02B)) ||
		((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FM) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_FM25G02C)))
	{
		if(((status & _SPI_NAND_VAL_ECC_STATUS_MASK_70) >> _SPI_NAND_VAL_ECC_STATUS_SHIFT_4) == _SPI_NAND_VAL_ECC_UNCORRECTABLE_3BIT)
		{
			rtn_status = SPI_NAND_FLASH_RTN_DETECTED_BAD_BLOCK;
		}
	}
	else if(((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_XTX) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_XT26G02B)))
	{
		if(((status & _SPI_NAND_VAL_ECC_STATUS_MASK_70) >> _SPI_NAND_VAL_ECC_STATUS_SHIFT_4) == _SPI_NAND_VAL_ECC_UNCORRECTABLE_3BIT)
		{
			rtn_status = SPI_NAND_FLASH_RTN_DETECTED_BAD_BLOCK;
		}
	}
	else if (((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_XTX) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_XT26G01C)))
	{
		if(((status & _SPI_NAND_VAL_ECC_STATUS_MASK_F0) >> _SPI_NAND_VAL_ECC_STATUS_SHIFT_4) == _SPI_NAND_VAL_ECC_UNCORRECTABLE_XTX_C)
		{
			rtn_status = SPI_NAND_FLASH_RTN_DETECTED_BAD_BLOCK;
		}
	}
	else if (((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_XTX) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_XT26G02C)))
	{
		if(((status & _SPI_NAND_VAL_ECC_STATUS_MASK_F0) >> _SPI_NAND_VAL_ECC_STATUS_SHIFT_4) == _SPI_NAND_VAL_ECC_UNCORRECTABLE_XTX_C)
		{
			rtn_status = SPI_NAND_FLASH_RTN_DETECTED_BAD_BLOCK;
		}
	}
	else if (((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_XTX) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_XT26G01A)))
	{
		if(((status & _SPI_NAND_VAL_ECC_STATUS_MASK_3C) >> _SPI_NAND_VAL_ECC_STATUS_SHIFT_2) == _SPI_NAND_VAL_ECC_UNCORRECTABLE_XTX_A)
		{
			rtn_status = SPI_NAND_FLASH_RTN_DETECTED_BAD_BLOCK;
		}
	}
	else if (((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_XTX) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_XT26G02A)))
	{
		if(((status & _SPI_NAND_VAL_ECC_STATUS_MASK_30) >> _SPI_NAND_VAL_ECC_STATUS_SHIFT_4) == _SPI_NAND_VAL_ECC_UNCORRECTABLE_2BIT)
		{
			rtn_status = SPI_NAND_FLASH_RTN_DETECTED_BAD_BLOCK;
		}
	}
	else if(((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_MIRA) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_PSU1GS20BN)))
	{
		if(((status & _SPI_NAND_VAL_ECC_STATUS_MASK_30) >> _SPI_NAND_VAL_ECC_STATUS_SHIFT_4) == _SPI_NAND_VAL_ECC_UNCORRECTABLE_2BIT )
		{
			rtn_status = SPI_NAND_FLASH_RTN_DETECTED_BAD_BLOCK;
		}
	}
	else if (((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_BIWIN) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_BWJX08U)) ||
		((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_BIWIN) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_BWET08U)))
	{
		if(((status & _SPI_NAND_VAL_ECC_STATUS_MASK_30) >> _SPI_NAND_VAL_ECC_STATUS_SHIFT_4) == _SPI_NAND_VAL_ECC_UNCORRECTABLE_2BIT)
		{
			rtn_status = SPI_NAND_FLASH_RTN_DETECTED_BAD_BLOCK;
		}
	}
	else if (((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FORESEE) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_FS35ND02GS2F1)) ||
		((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FORESEE) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_FS35ND02GD1F1)) ||
		((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FORESEE) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_FS35ND01GS1F1)) ||
		((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FORESEE) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_FS35ND01GD1F1)) ||
		((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FORESEE) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_FS35ND01GS1Y2)) ||
		((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FORESEE) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_FS35ND02GS3Y2)) ||
		((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FORESEE) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_FS35ND04GS2Y2)) ||
		((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FORESEE) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_F35UQA512M)) ||
		((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FORESEE) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_F35UQA001G)) ||
		((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FORESEE) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_F35UQA002G)) ||
		((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FORESEE) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_F35SQA512M)) ||
		((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FORESEE) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_F35SQA001G)) ||
		((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FORESEE) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_F35SQA002G)))
	{
		if(((status & _SPI_NAND_VAL_ECC_STATUS_MASK_70) >> _SPI_NAND_VAL_ECC_STATUS_SHIFT_4) == _SPI_NAND_VAL_ECC_UNCORRECTABLE_3BIT)
		{
			rtn_status = SPI_NAND_FLASH_RTN_DETECTED_BAD_BLOCK;
		}
	}
	else if(((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_DS) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_DS35Q2GA)) ||
		((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_DS) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_DS35M2GA)) ||
		((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_DS) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_DS35Q1GA)) ||
		((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_DS) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_DS35M1GA)) ||
		((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_DS) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_DS35Q2GB)) ||
		((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_DS) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_DS35M2GB)))
	{
		if(((status & _SPI_NAND_VAL_ECC_STATUS_MASK_30) >> _SPI_NAND_VAL_ECC_STATUS_SHIFT_4) == _SPI_NAND_VAL_ECC_UNCORRECTABLE_2BIT)
		{
			rtn_status = SPI_NAND_FLASH_RTN_DETECTED_BAD_BLOCK;
		}
	}
	else if(((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FISON) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_CS11G0T0A0AA)) ||
		((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FISON) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_CS11G1T0A0AA)) ||
		((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FISON) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_CS11G0G0A0AA)))
	{
		if(((status & _SPI_NAND_VAL_ECC_STATUS_MASK_70) >> _SPI_NAND_VAL_ECC_STATUS_SHIFT_4) == _SPI_NAND_VAL_ECC_UNCORRECTABLE_3BIT)
		{
			rtn_status = SPI_NAND_FLASH_RTN_DETECTED_BAD_BLOCK;
		}
	}
	else if(((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_TYM) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_TYM25D2GA01)) ||
		((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_TYM) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_TYM25D2GA02)) ||
		((ptr_dev_info_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_TYM) && (ptr_dev_info_t->dev_id == _SPI_NAND_DEVICE_ID_TYM25D1GA03)))
	{
		if(((status & _SPI_NAND_VAL_ECC_STATUS_MASK_30) >> _SPI_NAND_VAL_ECC_STATUS_SHIFT_4) == _SPI_NAND_VAL_ECC_UNCORRECTABLE_2BIT)
		{
			rtn_status = SPI_NAND_FLASH_RTN_DETECTED_BAD_BLOCK;
		}
	}

	if(rtn_status == SPI_NAND_FLASH_RTN_DETECTED_BAD_BLOCK)
	{
		fprintf(stderr, "[ecc_fail_check] : ECC cannot recover detected!, page = 0x%x\n", page_number);
	}

	return (rtn_status);
}

/* To load page into SPI NAND chip. */
static SPI_NAND_FLASH_RTN_T spi_nand_load_page_into_cache( u32 page_number)
{
	u8 status;
	SPI_NAND_FLASH_RTN_T rtn_status = SPI_NAND_FLASH_RTN_NO_ERROR;

	_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1,
	    "spi_nand_load_page_into_cache: page number = 0x%x\n", page_number);

	if (_current_page_num == page_number) {
	    _SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1,
	        "spi_nand_load_page_into_cache: page number == _current_page_num\n");
	} else {
	    spi_nand_select_die(page_number);
	    spi_nand_protocol_page_read(page_number);

	    // Check status for load page/erase/program complete
	    do {
	        spi_nand_protocol_get_status_reg_3(&status);
	    } while (status & _SPI_NAND_VAL_OIP);

	    _SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1,
	        "spi_nand_load_page_into_cache: status = 0x%x\n", status);

	    rtn_status = (ECC_fcheck && !ECC_ignore) ? ecc_fail_check(page_number) : 0;
	}

	return rtn_status;
}

// Function to set the SPI clock speed.
static void spi_nand_set_clock_speed( u32 clk)
{

}

// Function to check if the address and length are block-aligned.
static SPI_NAND_FLASH_RTN_T spi_nand_block_aligned_check( u32 addr, u32 len )
{
	struct SPI_NAND_FLASH_INFO_T *ptr_dev_info_t;
	SPI_NAND_FLASH_RTN_T rtn_status = SPI_NAND_FLASH_RTN_NO_ERROR;

	ptr_dev_info_t = _SPI_NAND_GET_DEVICE_INFO_PTR ;

	_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "SPI_NAND_BLOCK_ALIGNED_CHECK_check: addr = 0x%x, len = 0x%x, block size = 0x%x \n", addr, len, (ptr_dev_info_t->erase_size));

	if (_SPI_NAND_BLOCK_ALIGNED_CHECK(len, (ptr_dev_info_t->erase_size)))
	{
		len = ( (len/ptr_dev_info_t->erase_size) + 1) * (ptr_dev_info_t->erase_size);
		_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "SPI_NAND_BLOCK_ALIGNED_CHECK_check: erase block aligned first check OK, addr:%x len:%x\n", addr, len, (ptr_dev_info_t->erase_size));
	}

	if (_SPI_NAND_BLOCK_ALIGNED_CHECK(addr, (ptr_dev_info_t->erase_size)) || _SPI_NAND_BLOCK_ALIGNED_CHECK(len, (ptr_dev_info_t->erase_size)) )
	{
		_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "SPI_NAND_BLOCK_ALIGNED_CHECK_check: erase block not aligned, addr:0x%x len:0x%x, blocksize:0x%x\n", addr, len, (ptr_dev_info_t->erase_size));
		rtn_status = SPI_NAND_FLASH_RTN_ALIGNED_CHECK_FAIL;
	}

	return (rtn_status);
}

SPI_NAND_FLASH_RTN_T spi_nand_erase_block ( u32 block_index)
{
	u8 status;
	SPI_NAND_FLASH_RTN_T rtn_status = SPI_NAND_FLASH_RTN_NO_ERROR;

	spi_nand_select_die ( (block_index << _SPI_NAND_BLOCK_ROW_ADDRESS_OFFSET) );

	/* 2.2 Enable write_to flash */
	spi_nand_protocol_write_enable();

	/* 2.3 Erasing one block */
	spi_nand_protocol_block_erase( block_index );

	/* 2.4 Checking status for erase complete */
	do {
		spi_nand_protocol_get_status_reg_3( &status);
	} while( status & _SPI_NAND_VAL_OIP) ;

	/* 2.5 Disable write_flash */
	spi_nand_protocol_write_disable();

	/* 2.6 Check Erase Fail Bit */
	if( status & _SPI_NAND_VAL_ERASE_FAIL )
	{
		fprintf(stderr, "spi_nand_erase_block : erase block fail, block = 0x%x, status = 0x%x\n", block_index, status); // Use stderr
		rtn_status = SPI_NAND_FLASH_RTN_ERASE_FAIL;
	}

	return rtn_status;
}

// Function to erase flash internally.
static SPI_NAND_FLASH_RTN_T spi_nand_erase_internal( u32 addr, u32 len )
{
	u32 block_index = 0;
	u32 erase_len = 0;
	SPI_NAND_FLASH_RTN_T rtn_status = SPI_NAND_FLASH_RTN_NO_ERROR;

	_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "\nspi_nand_erase_internal (in): addr = 0x%x, len = 0x%x\n", addr, len );
	_SPI_NAND_SEMAPHORE_LOCK();

	/* Switch to manual mode*/
	_SPI_NAND_ENABLE_MANUAL_MODE();

	SPI_NAND_Flash_Clear_Read_Cache_Data();

	/* 1. Check the address and len must aligned to NAND Flash block size */
	if( spi_nand_block_aligned_check( addr, len) == SPI_NAND_FLASH_RTN_NO_ERROR)
	{
		/* 2. Erase block one by one */
		while( erase_len < len )
		{
			/* 2.1 Caculate Block index */
			block_index = (addr/(_current_flash_info_t.erase_size));

			_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "spi_nand_erase_internal: addr = 0x%x, len = 0x%x, block_idx = 0x%x\n", addr, len, block_index );

			rtn_status = spi_nand_erase_block(block_index);

			/* 2.6 Check Erase Fail Bit */
			if(rtn_status != SPI_NAND_FLASH_RTN_NO_ERROR)
			{
				// This message might be informational depending on context, keep as _SPI_NAND_PRINTF for now.
				// If it's definitely an error leading to failure, change to fprintf(stderr, ...).
				_SPI_NAND_PRINTF("spi_nand_erase_internal : Erase Fail at addr = 0x%x, len = 0x%x, block_idx = 0x%x\n", addr, len, block_index);
				// rtn_status is already set by spi_nand_erase_block if it failed
			}

			/* 2.7 Erase next block if needed */
			addr		+= _current_flash_info_t.erase_size;
			erase_len	+= _current_flash_info_t.erase_size;
			if( timer_progress() )
			{
				printf("\bErase %d%% [%u] of [%u] bytes      ", 100 * (erase_len / 1024) / (len / 1024), erase_len, len);
				printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");
				fflush(stdout);
			}
		}
		printf("Erase 100%% [%u] of [%u] bytes      \n", erase_len, len);
	}
	else
	{
		rtn_status = SPI_NAND_FLASH_RTN_ALIGNED_CHECK_FAIL;
	}

	_SPI_NAND_SEMAPHORE_UNLOCK();

	return 	(rtn_status);
}

static SPI_NAND_FLASH_RTN_T spi_nand_read_page (u32 page_number, SPI_NAND_FLASH_READ_SPEED_MODE_T speed_mode)
{
	struct SPI_NAND_FLASH_INFO_T *ptr_dev_info_t;
	SPI_NAND_FLASH_RTN_T rtn_status = SPI_NAND_FLASH_RTN_NO_ERROR;
	u16 read_addr;

	ptr_dev_info_t = _SPI_NAND_GET_DEVICE_INFO_PTR;

	/* read from read_addr index in the page */
	read_addr = 0;

	/* Switch to manual mode*/
	_SPI_NAND_ENABLE_MANUAL_MODE();

	/* 1. Load Page into cache of NAND Flash Chip */
	rtn_status = spi_nand_load_page_into_cache(page_number); // Store return status
	if( rtn_status == SPI_NAND_FLASH_RTN_DETECTED_BAD_BLOCK )
	{
		// Error message already printed by ecc_fail_check to stderr if applicable
		// _SPI_NAND_PRINTF("spi_nand_read_page: Bad Block, ECC cannot recovery detected, page = 0x%x\n", page_number);
		// rtn_status is already set
	}

	/* 2. Read whole data from cache of NAND Flash Chip */
	_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "spi_nand_read_page: curren_page_num = 0x%x, page_number = 0x%x\n", _current_page_num, page_number);

	/* No matter what status, we must read the cache data to dram */
	if((_current_page_num != page_number))
	{
		memset(_current_cache_page, 0x0, sizeof(_current_cache_page));

		_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_2, "spi_nand_read_page: before read, _current_cache_page:\n");
		_SPI_NAND_DEBUG_PRINTF_ARRAY(SPI_NAND_FLASH_DEBUG_LEVEL_2, &_current_cache_page[0], _SPI_NAND_CACHE_SIZE);

		if( ((ptr_dev_info_t->feature) & SPI_NAND_FLASH_PLANE_SELECT_HAVE) )
		{
			_plane_select_bit = ((page_number >> 6)& (0x1));

			_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1,"spi_nand_read_page: plane select = 0x%x\n",  _plane_select_bit);
		}

		{
			spi_nand_protocol_read_from_cache(read_addr, ((ptr_dev_info_t->page_size)+(ptr_dev_info_t->oob_size)), &_current_cache_page[0], speed_mode, ptr_dev_info_t->dummy_mode );
		}

		_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_2, "spi_nand_read_page: after read, _current_cache_page:\n");
		_SPI_NAND_DEBUG_PRINTF_ARRAY(SPI_NAND_FLASH_DEBUG_LEVEL_2, &_current_cache_page[0], _SPI_NAND_CACHE_SIZE);

		/* Divide read page into data segment and oob segment  */
		memcpy( &_current_cache_page_data[0], &_current_cache_page[0], (ptr_dev_info_t->page_size) );

		if(ECC_fcheck) {
			memcpy( &_current_cache_page_oob[0],  &_current_cache_page[(ptr_dev_info_t->page_size)], (ptr_dev_info_t->oob_size) );
			memcpy( &_current_cache_page_oob_mapping[0],  &_current_cache_page_oob[0], (ptr_dev_info_t->oob_size) );
		}

		_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_2, "spi_nand_read_page: _current_cache_page:\n");
		_SPI_NAND_DEBUG_PRINTF_ARRAY(SPI_NAND_FLASH_DEBUG_LEVEL_2, &_current_cache_page[0], ((ptr_dev_info_t->page_size)+(ptr_dev_info_t->oob_size)));
		_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_2, "spi_nand_read_page: _current_cache_page_oob:\n");
		_SPI_NAND_DEBUG_PRINTF_ARRAY(SPI_NAND_FLASH_DEBUG_LEVEL_2, &_current_cache_page_oob[0], (ptr_dev_info_t->oob_size));
		_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_2, "spi_nand_read_page: _current_cache_page_oob_mapping:\n");
		_SPI_NAND_DEBUG_PRINTF_ARRAY(SPI_NAND_FLASH_DEBUG_LEVEL_2, &_current_cache_page_oob_mapping[0], (ptr_dev_info_t->oob_size));

		_current_page_num = page_number;
	}

	return rtn_status;
}

static SPI_NAND_FLASH_RTN_T spi_nand_write_page( u32 page_number, u32 data_offset, u8  *ptr_data, u32 data_len, u32 oob_offset, u8  *ptr_oob,
											u32 oob_len, SPI_NAND_FLASH_WRITE_SPEED_MODE_T speed_mode )
{
		u8 status, status_2;
		struct SPI_NAND_FLASH_INFO_T *ptr_dev_info_t;
		SPI_NAND_FLASH_RTN_T rtn_status = SPI_NAND_FLASH_RTN_NO_ERROR;
		u16 write_addr;

		int only_ffff = 1;

		for (int i=0; i<data_len; i++){
			if (ptr_data[i] != 0xff) {
				only_ffff = 0;
				break;
			}
		}

		if (only_ffff) {
			return 0;
		}

		/* write to write_addr index in the page */
		write_addr = 0;

		/* Switch to manual mode*/
		_SPI_NAND_ENABLE_MANUAL_MODE();

		ptr_dev_info_t = _SPI_NAND_GET_DEVICE_INFO_PTR;

		/* Read Current page data to software cache buffer */
		rtn_status = spi_nand_read_page(page_number, speed_mode);
		if (Skip_BAD_page && (rtn_status == SPI_NAND_FLASH_RTN_DETECTED_BAD_BLOCK)) { /* skip BAD page, go to next page */
			return SPI_NAND_FLASH_RTN_DETECTED_BAD_BLOCK;
		}

		/* Rewrite the software cache buffer */
		if(data_len > 0)
		{
			memcpy( &_current_cache_page_data[data_offset], &ptr_data[0], data_len );
		}

		memcpy( &_current_cache_page[0], &_current_cache_page_data[0], ptr_dev_info_t->page_size);

		if(ECC_fcheck && oob_len > 0 )	/* Write OOB */
		{
			memcpy( &_current_cache_page_oob[0], &ptr_oob[0], oob_len);
			if(ptr_dev_info_t->oob_size)
				memcpy( &_current_cache_page[ptr_dev_info_t->page_size],  &_current_cache_page_oob[0], ptr_dev_info_t->oob_size );
		}

		_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_2, "spi_nand_write_page: page = 0x%x, data_offset = 0x%x, date_len = 0x%x, oob_offset = 0x%x, oob_len = 0x%x\n", page_number, data_offset, data_len, oob_offset, oob_len);
		_SPI_NAND_DEBUG_PRINTF_ARRAY(SPI_NAND_FLASH_DEBUG_LEVEL_2, &_current_cache_page[0], ((ptr_dev_info_t->page_size) + (ptr_dev_info_t->oob_size)));

		if( ((ptr_dev_info_t->feature) & SPI_NAND_FLASH_PLANE_SELECT_HAVE) )
		{
			_plane_select_bit = ((page_number >> 6) & (0x1));

			_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_2, "spi_nand_write_page: _plane_select_bit = 0x%x\n", _plane_select_bit );
		}

		spi_nand_select_die ( page_number );

		/* Different Manafacture have different prgoram flow and setting */
		if( ((ptr_dev_info_t->mfr_id) == _SPI_NAND_MANUFACTURER_ID_GIGADEVICE) ||
			((ptr_dev_info_t->mfr_id) == _SPI_NAND_MANUFACTURER_ID_PN) ||
			((ptr_dev_info_t->mfr_id) == _SPI_NAND_MANUFACTURER_ID_FM) ||
			((ptr_dev_info_t->mfr_id) == _SPI_NAND_MANUFACTURER_ID_XTX) ||
			((ptr_dev_info_t->mfr_id) == _SPI_NAND_MANUFACTURER_ID_FORESEE) ||
			((ptr_dev_info_t->mfr_id) == _SPI_NAND_MANUFACTURER_ID_FISON) ||
			((ptr_dev_info_t->mfr_id) == _SPI_NAND_MANUFACTURER_ID_TYM) ||
			((ptr_dev_info_t->mfr_id) == _SPI_NAND_MANUFACTURER_ID_ATO_2) ||
			(((ptr_dev_info_t->mfr_id) == _SPI_NAND_MANUFACTURER_ID_ATO) && ((ptr_dev_info_t->dev_id) == _SPI_NAND_DEVICE_ID_ATO25D2GA)))
		{
			{
				spi_nand_protocol_program_load(write_addr, &_current_cache_page[0], ((ptr_dev_info_t->page_size) + (ptr_dev_info_t->oob_size)), speed_mode);
			}

			/* Enable write_to flash */
			spi_nand_protocol_write_enable();
		}
		else
		{
			/* Enable write_to flash */
			spi_nand_protocol_write_enable();

			{
				/* Program data into buffer of SPI NAND chip */
				spi_nand_protocol_program_load(write_addr, &_current_cache_page[0], ((ptr_dev_info_t->page_size) + (ptr_dev_info_t->oob_size)), speed_mode);
			}
		}

		/* Execute program data into SPI NAND chip  */
		spi_nand_protocol_program_execute ( page_number );

		/* Checking status for erase complete */
		do {
			spi_nand_protocol_get_status_reg_3( &status);
		} while( status & _SPI_NAND_VAL_OIP) ;

		/*. Disable write_flash */
		spi_nand_protocol_write_disable();

		spi_nand_protocol_get_status_reg_1( &status_2);

		_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "[spi_nand_write_page]: status 1 = 0x%x, status 3 = 0x%x\n", status_2, status);
		/* Check Program Fail Bit */
		if( status & _SPI_NAND_VAL_PROGRAM_FAIL )
		{
			fprintf(stderr, "spi_nand_write_page : Program Fail at addr_offset = 0x%x, page_number = 0x%x, status = 0x%x\n", data_offset, page_number, status); // Use stderr
			rtn_status = SPI_NAND_FLASH_RTN_PROGRAM_FAIL;
		}

		SPI_NAND_Flash_Clear_Read_Cache_Data();

		return (rtn_status);
}

int test_write_fail_flag = 0;

// Internal function to write data to SPI NAND flash.
static SPI_NAND_FLASH_RTN_T spi_nand_write_internal( u32 dst_addr, u32 len, u32 *ptr_rtn_len, u8* ptr_buf, SPI_NAND_FLASH_WRITE_SPEED_MODE_T speed_mode )
{
	u32 remain_len, write_addr, data_len, page_number, physical_dst_addr;
	u32 addr_offset;
	struct SPI_NAND_FLASH_INFO_T *ptr_dev_info_t;
	SPI_NAND_FLASH_RTN_T rtn_status = SPI_NAND_FLASH_RTN_NO_ERROR;

	*ptr_rtn_len = 0;
	ptr_dev_info_t = _SPI_NAND_GET_DEVICE_INFO_PTR;
	remain_len = len;
	write_addr = dst_addr;

	_SPI_NAND_SEMAPHORE_LOCK();

	SPI_NAND_Flash_Clear_Read_Cache_Data();

	_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "spi_nand_write_internal: remain_len = 0x%x\n", remain_len);

	while( remain_len > 0 )
	{
		physical_dst_addr = write_addr;

		/* Calculate page number */
		addr_offset = (physical_dst_addr % (ptr_dev_info_t->page_size));
		page_number = (physical_dst_addr / (ptr_dev_info_t->page_size));

		_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "\nspi_nand_write_internal: addr_offset = 0x%x, page_number = 0x%x, remain_len = 0x%x, page_size = 0x%x\n", addr_offset, page_number, remain_len,(ptr_dev_info_t->page_size) );
		if( ((addr_offset + remain_len ) > (ptr_dev_info_t->page_size))  )  /* data cross over than 1-page range */
		{
			data_len = ((ptr_dev_info_t->page_size) - addr_offset);
		}
		else
		{
			data_len = remain_len;
		}

		rtn_status = spi_nand_write_page(page_number, addr_offset, &(ptr_buf[len - remain_len]), data_len, 0, NULL, 0 , speed_mode);
		/* skip BAD page or internal error on write page, go to next page */
		if( Skip_BAD_page && ((rtn_status == SPI_NAND_FLASH_RTN_PROGRAM_FAIL) || (rtn_status == SPI_NAND_FLASH_RTN_DETECTED_BAD_BLOCK)) ) {
			if( ((addr_offset + remain_len ) < (ptr_dev_info_t->page_size)) )
				break;
			write_addr += data_len;
			continue;
		}

		/* 8. Write remain data if necessary */
		write_addr += data_len;
		remain_len -= data_len;
		ptr_rtn_len += data_len;
		if( timer_progress() )
		{
			printf("\bWritten %d%% [%u] of [%u] bytes      ", 100 * ((len - remain_len) / 1024) / (len / 1024), len - remain_len, len);
			printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");
			fflush(stdout);
		}
	}
	printf("Written 100%% [%u] of [%u] bytes      \n", len - remain_len, len);
	_SPI_NAND_SEMAPHORE_UNLOCK();

	return (rtn_status);
}

// Placeholder for spi_nand_read_internal function.
static SPI_NAND_FLASH_RTN_T spi_nand_read_internal ( u32 addr, u32 len, u8 *ptr_rtn_buf, SPI_NAND_FLASH_READ_SPEED_MODE_T speed_mode,
									SPI_NAND_FLASH_RTN_T *status)
{
	u32 page_number, data_offset;
	u32 read_addr, physical_read_addr, remain_len;
	struct SPI_NAND_FLASH_INFO_T *ptr_dev_info_t;
	SPI_NAND_FLASH_RTN_T rtn_status = SPI_NAND_FLASH_RTN_NO_ERROR;

	ptr_dev_info_t = _SPI_NAND_GET_DEVICE_INFO_PTR;

	read_addr = addr;
	remain_len = len;

	_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "\nspi_nand_read_internal : addr = 0x%lx, len = 0x%x\n", addr, len );

	_SPI_NAND_SEMAPHORE_LOCK();

	*status = SPI_NAND_FLASH_RTN_NO_ERROR;

	while(remain_len > 0)
	{
		physical_read_addr = read_addr;

		/* Calculate page number */
		data_offset = (physical_read_addr % (ptr_dev_info_t->page_size));
		page_number = (physical_read_addr / (ptr_dev_info_t->page_size));

		_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "spi_nand_read_internal: read_addr = 0x%x, page_number = 0x%x, data_offset = 0x%x\n", physical_read_addr, page_number, data_offset);

		rtn_status = spi_nand_read_page(page_number, speed_mode);
		if(rtn_status == SPI_NAND_FLASH_RTN_DETECTED_BAD_BLOCK) {
			if (!Skip_BAD_page) {
				*status = SPI_NAND_FLASH_RTN_DETECTED_BAD_BLOCK;
				return (rtn_status);
			}
			/* skip BAD page, go to next page */
			if( (data_offset + remain_len) < ptr_dev_info_t->page_size )
				break;
			read_addr += (ptr_dev_info_t->page_size - data_offset);
			continue;
		}

		/* 3. Retrieve the request data */
		if( (data_offset + remain_len) < ptr_dev_info_t->page_size )
		{
			memcpy( &ptr_rtn_buf[len - remain_len], &_current_cache_page_data[data_offset], (sizeof(unsigned char)*remain_len) );
			remain_len = 0;
		}
		else
		{
			memcpy( &ptr_rtn_buf[len - remain_len], &_current_cache_page_data[data_offset], (sizeof(unsigned char)*(ptr_dev_info_t->page_size - data_offset)));
			remain_len -= (ptr_dev_info_t->page_size - data_offset);
			read_addr += (ptr_dev_info_t->page_size - data_offset);
		}
		if( timer_progress() )
		{
			printf("\bRead %d%% [%u] of [%u] bytes      ", 100 * ((len - remain_len) / 1024) / (len / 1024), len - remain_len, len);
			printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");
			fflush(stdout);
		}
	}
	printf("Read 100%% [%u] of [%u] bytes      \n", len - remain_len, len);
	_SPI_NAND_SEMAPHORE_UNLOCK();

	return (rtn_status);
}

// Function to initialize the SPI NAND Flash chip.
static void spi_nand_manufacturer_init( struct SPI_NAND_FLASH_INFO_T *ptr_device_t )
{
	unsigned char feature;

	_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1,"SPI NAND Chip Init : Unlock all block and Enable Quad Mode\n");

	if(((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_GIGADEVICE) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F1GQ4UAYIG)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_GIGADEVICE) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F1GQ4UBYIG)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_GIGADEVICE) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F1GQ4REYIG)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_GIGADEVICE) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F1GQ4UCYIG)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_GIGADEVICE) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F1GQ4UEYIS)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_GIGADEVICE) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F1GQ5UEYIG)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_GIGADEVICE) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F1GQ5REYIG)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_GIGADEVICE) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F2GQ4UBYIG)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_GIGADEVICE) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F2GQ4UE9IS)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_GIGADEVICE) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F2GQ4UCYIG)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_GIGADEVICE) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F4GQ4UBYIG)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_GIGADEVICE) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F4GQ4UCYIG)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_GIGADEVICE) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F2GQ5UEYIG)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_GIGADEVICE) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F2GQ5REYIG)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_GIGADEVICE) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F1GM7UEYIG)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_GIGADEVICE) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F1GM7REYIG)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_GIGADEVICE) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F2GM7UEYIG)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_GIGADEVICE) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_GD5F2GM7REYIG)))
	{
		/* 1. Unlock All block */
		spi_nand_protocol_get_status_reg_1(&feature);
		feature &= 0xC1;
		spi_nand_protocol_set_status_reg_1(feature);

		spi_nand_protocol_get_status_reg_1(&feature);
		_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "After Unlock all block setup, the status register1 = 0x%x\n", feature);

		/* 2. Enable Qual mode */
		spi_nand_protocol_get_status_reg_2(&feature);
		feature |= 0x1;
		spi_nand_protocol_set_status_reg_2(feature);

		spi_nand_protocol_get_status_reg_2(&feature);
		_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "After enable qual mode setup, the status register2 = 0x%x\n", feature);
	}
	else if((ptr_device_t->mfr_id) == _SPI_NAND_MANUFACTURER_ID_MXIC)
	{
		/* 1. Unlock All block */
		spi_nand_protocol_get_status_reg_1(&feature);
		feature &= 0xC1;
		spi_nand_protocol_set_status_reg_1(feature);

		spi_nand_protocol_get_status_reg_1(&feature);
		_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "After Unlock all block setup, the status register1 = 0x%x\n", feature);

		/* 2. Enable Qual mode */
		spi_nand_protocol_get_status_reg_2(&feature);
		feature |= 0x1;
		spi_nand_protocol_set_status_reg_2(feature);

		spi_nand_protocol_get_status_reg_2(&feature);
		_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "After enable qual mode setup, the status register2 = 0x%x\n", feature);
	}
	else if( (ptr_device_t->mfr_id) == _SPI_NAND_MANUFACTURER_ID_WINBOND)
	{
		if(((ptr_device_t->feature) & SPI_NAND_FLASH_DIE_SELECT_1_HAVE)) {
			_die_id = 0x00;
			spi_nand_protocol_die_select_1(_die_id);
		}

		/* Enable to modify the status regsiter 1 */
		feature = 0x58;
		spi_nand_protocol_set_status_reg_2(feature);

		/* Unlock all block and Enable Qual mode */
		feature = 0x81;
		spi_nand_protocol_set_status_reg_1(feature);

		/* Disable to modify the status regsiter 1 */
		feature = 0x18;
		spi_nand_protocol_set_status_reg_2(feature);

		spi_nand_protocol_get_status_reg_1(&feature);

		_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "After Unlock all block setup, the status register1 = 0x%x\n", feature);

		/* Unlock all block for Die_1 */
		if( ((ptr_device_t->feature) & SPI_NAND_FLASH_DIE_SELECT_1_HAVE) )
		{
			_die_id = 0x01;
			spi_nand_protocol_die_select_1(_die_id);

			/* Enable to modify the status regsiter 1 */
			feature = 0x58;
			spi_nand_protocol_set_status_reg_2(feature);

			/* Unlock all block and Enable Qual mode */
			feature = 0x81;
			spi_nand_protocol_set_status_reg_1(feature);

			/* Disable to modify the status regsiter 1 */
			feature = 0x18;
			spi_nand_protocol_set_status_reg_2(feature);

			spi_nand_protocol_get_status_reg_1(&feature);

			_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "After Unlock all block setup, the die %d status register1 = 0x%x\n", _die_id, feature);
		}
	}
	else if(((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_ESMT) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_F50L512M41A)) ||
			((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_ESMT) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_F50L1G41A0)))
	{
		/* 1. Unlock All block */
		spi_nand_protocol_get_status_reg_1(&feature);
		feature &= 0xC7;
		spi_nand_protocol_set_status_reg_1(feature);

		spi_nand_protocol_get_status_reg_1(&feature);

		_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "After Unlock all block setup, the status register1 = 0x%x\n", feature);
	}
	else if(((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_ESMT) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_F50D1G41LB)) ||
			((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_ESMT) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_F50L1G41LB)) ||
			((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_ESMT) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_F50L2G41LB)))
	{
		if(((ptr_device_t->feature) & SPI_NAND_FLASH_DIE_SELECT_1_HAVE))
		{
			_die_id = 0x00;
			spi_nand_protocol_die_select_1(_die_id);
		}

		/* 1. Unlock All block */
		feature = 0x83;
		spi_nand_protocol_set_status_reg_1(feature);

		spi_nand_protocol_get_status_reg_1(&feature);

		_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "After Unlock all block setup, the status register1 = 0x%x\n", feature);

		/* Unlock all block for Die_1 */
		if(((ptr_device_t->feature) & SPI_NAND_FLASH_DIE_SELECT_1_HAVE))
		{
			_die_id = 0x01;
			spi_nand_protocol_die_select_1(_die_id);

			/* 1. Unlock All block */
			feature = 0x83;
			spi_nand_protocol_set_status_reg_1(feature);

			spi_nand_protocol_get_status_reg_1(&feature);

			_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "After Unlock all block setup, the die %d status register1 = 0x%x\n", _die_id, feature);
		}
	}
	else if(((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_ZENTEL) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_A5U12A21ASC)) ||
			((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_ZENTEL) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_A5U1GA21BWS)))
	{
		/* 1. Unlock All block */
		spi_nand_protocol_get_status_reg_1(&feature);
		feature &= 0xC7;
		spi_nand_protocol_set_status_reg_1(feature);

		spi_nand_protocol_get_status_reg_1(&feature);
		_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "After Unlock all block setup, the status register1 = 0x%x\n", feature);
	}
	else if( (ptr_device_t->mfr_id) == _SPI_NAND_MANUFACTURER_ID_ETRON)
	{
		/* 1. Unlock All block */
		spi_nand_protocol_get_status_reg_1(&feature);
		feature &= 0xC1;
		spi_nand_protocol_set_status_reg_1(feature);

		spi_nand_protocol_get_status_reg_1(&feature);
		_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "After Unlock all block setup, the status register1 = 0x%x\n", feature);

		/* 2. Enable Qual mode */
		spi_nand_protocol_get_status_reg_2(&feature);
		feature |= 0x1;
		spi_nand_protocol_set_status_reg_2(feature);

		spi_nand_protocol_get_status_reg_2(&feature);
		_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "After enable qual mode setup, the status register2 = 0x%x\n", feature);
	}
	else if( (ptr_device_t->mfr_id) == _SPI_NAND_MANUFACTURER_ID_TOSHIBA)
	{
		/* 1. Unlock All block */
		spi_nand_protocol_get_status_reg_1(&feature);
		feature &= 0xC7;
		spi_nand_protocol_set_status_reg_1(feature);

		spi_nand_protocol_get_status_reg_1(&feature);
		_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1,"After Unlock all block setup, the status register1 = 0x%x\n", feature);
	}

	else if( (ptr_device_t->mfr_id) == _SPI_NAND_MANUFACTURER_ID_MICRON)
	{
		if(((ptr_device_t->feature) & SPI_NAND_FLASH_DIE_SELECT_2_HAVE)) {
			_die_id = 0x00;
			spi_nand_protocol_die_select_2(_die_id);
		}

		/* 1. Unlock All block */
		spi_nand_protocol_get_status_reg_1(&feature);
		feature &= 0x83;
		spi_nand_protocol_set_status_reg_1(feature);

		spi_nand_protocol_get_status_reg_1(&feature);
		_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1,"After Unlock all block setup, the status register1 = 0x%x\n", feature);

		/* Unlock all block for Die_1 */
		if( ((ptr_device_t->feature) & SPI_NAND_FLASH_DIE_SELECT_2_HAVE) )
		{
			_die_id = 0x01;
			spi_nand_protocol_die_select_2(_die_id);

			/* 1. Unlock All block */
			spi_nand_protocol_get_status_reg_1(&feature);
			feature &= 0x83;
			spi_nand_protocol_set_status_reg_1(feature);

			spi_nand_protocol_get_status_reg_1(&feature);
			_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1,"After Unlock all block setup, the die %d status register1 = 0x%x\n", _die_id, feature);
		}
	}
	else if((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_HEYANG) ||
		(ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_HEYANG_2))
	{
		/* 1. Unlock All block */
		spi_nand_protocol_get_status_reg_1(&feature);
		feature &= 0xC7;
		spi_nand_protocol_set_status_reg_1(feature);

		/* 2. Enable Qual mode */
		spi_nand_protocol_get_status_reg_2(&feature);
		feature |= 0x1;
		spi_nand_protocol_set_status_reg_2(feature);
	}
	else if(((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_PN) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_PN26G01AWSIUG)) ||
			((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_PN) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_PN26G02AWSIUG)) ||
			((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_PN) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_PN26Q01AWSIUG)))
	{
		/* 1. Unlock All block */
		spi_nand_protocol_get_status_reg_1(&feature);
		feature &= 0xC7;
		spi_nand_protocol_set_status_reg_1(feature);

		/* 2. Enable Qual mode */
		spi_nand_protocol_get_status_reg_2(&feature);
		feature |= 0x1;
		spi_nand_protocol_set_status_reg_2(feature);
	}
	else if( ((ptr_device_t->mfr_id) == _SPI_NAND_MANUFACTURER_ID_ATO) ||
		 ((ptr_device_t->mfr_id) == _SPI_NAND_MANUFACTURER_ID_ATO_2) )
	{
		/* 1. Unlock All block */
		spi_nand_protocol_get_status_reg_1(&feature);
		feature &= 0xC7;
		spi_nand_protocol_set_status_reg_1(feature);
	}
	else if(((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FM) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_FM25S01)) ||
			((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FM) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_FM25S01A)) ||
			((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FM) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_FM25S02A)))
	{
		/* 1. Unlock All block */
		spi_nand_protocol_get_status_reg_1(&feature);
		feature &= 0x87;
		spi_nand_protocol_set_status_reg_1(feature);
	}
	else if(((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FM) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_FM25G01B)) ||
			((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FM) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_FM25G02B)) ||
			((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FM) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_FM25G02C)))
	{
		/* 1. Unlock All block */
		spi_nand_protocol_get_status_reg_1(&feature);
		feature &= 0xC7;
		spi_nand_protocol_set_status_reg_1(feature);

		/* 2. Enable Qual mode */
		spi_nand_protocol_get_status_reg_2(&feature);
		feature |= 0x1;
		spi_nand_protocol_set_status_reg_2(feature);
	}
	else if(((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_XTX) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_XT26G02B)) ||
			((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_XTX) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_XT26G02A)) ||
			((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_XTX) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_XT26G02C)))
	{
		/* 1. Unlock All block */
		spi_nand_protocol_get_status_reg_1(&feature);
		feature &= 0xC7;
		spi_nand_protocol_set_status_reg_1(feature);

		/* 2. Enable Qual mode */
		spi_nand_protocol_get_status_reg_2(&feature);
		feature |= 0x1;
		spi_nand_protocol_set_status_reg_2(feature);
	}
	else if(((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_MIRA) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_PSU1GS20BN)))
	{
		/* 1. Unlock All block */
		spi_nand_protocol_get_status_reg_1(&feature);
		feature &= 0xC7;
		spi_nand_protocol_set_status_reg_1(feature);

		spi_nand_protocol_get_status_reg_1(&feature);

		_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "After Unlock all block setup, the status register1 = 0x%x\n", feature);
	}
	else if(((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_BIWIN) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_BWJX08U)) ||
			((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_BIWIN) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_BWET08U)))
	{
		/* 1. Unlock All block */
		spi_nand_protocol_get_status_reg_1(&feature);
		feature &= 0xC7;
		spi_nand_protocol_set_status_reg_1(feature);

		/* 2. Enable Qual mode */
		spi_nand_protocol_get_status_reg_2(&feature);
		feature |= 0x1;
		spi_nand_protocol_set_status_reg_2(feature);
	}
	else if(((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FORESEE) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_FS35ND02GS2F1)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FORESEE) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_FS35ND02GD1F1)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FORESEE) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_FS35ND01GS1F1)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FORESEE) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_FS35ND01GD1F1)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FORESEE) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_FS35ND01GS1Y2)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FORESEE) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_FS35ND02GS3Y2)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FORESEE) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_FS35ND04GS2Y2)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FORESEE) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_F35UQA512M)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FORESEE) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_F35UQA001G)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FORESEE) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_F35UQA002G)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FORESEE) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_F35SQA512M)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FORESEE) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_F35SQA001G)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FORESEE) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_F35SQA002G)))
	{
		/* 1. Unlock All block */
		spi_nand_protocol_get_status_reg_1(&feature);
		feature &= 0xC7;
		spi_nand_protocol_set_status_reg_1(feature);

		/* 2. Enable Qual mode */
		spi_nand_protocol_get_status_reg_2(&feature);
		feature |= 0x1;
		spi_nand_protocol_set_status_reg_2(feature);
	}
	else if(((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_DS) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_DS35Q2GA)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_DS) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_DS35M2GA)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_DS) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_DS35Q1GA)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_DS) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_DS35M1GA)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_DS) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_DS35Q2GB)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_DS) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_DS35M2GB)))
	{
		/* 1. Unlock All block */
		spi_nand_protocol_get_status_reg_1(&feature);
		feature &= 0xC7;
		spi_nand_protocol_set_status_reg_1(feature);

		spi_nand_protocol_get_status_reg_1(&feature);
		_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "After Unlock all block setup, the status register1 = 0x%x\n", feature);
	}
	else if(((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FISON) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_CS11G0T0A0AA)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FISON) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_CS11G1T0A0AA)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_FISON) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_CS11G0G0A0AA)))
	{
		/* 1. Unlock All block */
		spi_nand_protocol_get_status_reg_1(&feature);
		feature &= 0xC7;
		spi_nand_protocol_set_status_reg_1(feature);

		spi_nand_protocol_get_status_reg_1(&feature);
		_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "After Unlock all block setup, the status register1 = 0x%x\n", feature);
	}
	else if(((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_TYM) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_TYM25D2GA01)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_TYM) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_TYM25D2GA02)) ||
		((ptr_device_t->mfr_id == _SPI_NAND_MANUFACTURER_ID_TYM) && (ptr_device_t->dev_id == _SPI_NAND_DEVICE_ID_TYM25D1GA03)))
	{
		/* 1. Unlock All block */
		spi_nand_protocol_get_status_reg_1(&feature);
		feature &= 0xC7;
		spi_nand_protocol_set_status_reg_1(feature);

		spi_nand_protocol_get_status_reg_1(&feature);
		_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "After Unlock all block setup, the status register1 = 0x%x\n", feature);
	}
	else
	{
		/* 1. Unlock All block */
		spi_nand_protocol_get_status_reg_1(&feature);
		feature &= 0xC1;
		spi_nand_protocol_set_status_reg_1(feature);

		spi_nand_protocol_get_status_reg_1(&feature);
		_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "After Unlock all block setup, the status register1 = 0x%x\n", feature);

		/* 2. Enable Qual mode */
		spi_nand_protocol_get_status_reg_2(&feature);
		feature |= 0x1;
		spi_nand_protocol_set_status_reg_2(feature);

		spi_nand_protocol_get_status_reg_2(&feature);
		_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "After enable qual mode setup, the status register2 = 0x%x\n", feature);
	}
}

// Compare a read SPI NAND flash ID with a flash table entry ID.
static SPI_NAND_FLASH_RTN_T spi_nand_compare(
	const struct SPI_NAND_FLASH_INFO_T *ptr_rtn_device_t,
	const struct SPI_NAND_FLASH_INFO_T *spi_nand_flash_table)
{
	if (spi_nand_flash_table->dev_id_2 == 0) {
		_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1,
			"spi_nand_compare: mfr_id = 0x%x, dev_id = 0x%x\n",
			spi_nand_flash_table->mfr_id, spi_nand_flash_table->dev_id);

		if ((ptr_rtn_device_t->mfr_id == spi_nand_flash_table->mfr_id) &&
			(ptr_rtn_device_t->dev_id == spi_nand_flash_table->dev_id)) {
			return SPI_NAND_FLASH_RTN_NO_ERROR;
		}
	} else {
		_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1,
			"spi_nand_compare: mfr_id = 0x%x, dev_id = 0x%x, dev_id_2 = 0x%x\n",
			spi_nand_flash_table->mfr_id, spi_nand_flash_table->dev_id, spi_nand_flash_table->dev_id_2);

		if ((ptr_rtn_device_t->mfr_id == spi_nand_flash_table->mfr_id) &&
			(ptr_rtn_device_t->dev_id == spi_nand_flash_table->dev_id) &&
			(ptr_rtn_device_t->dev_id_2 == spi_nand_flash_table->dev_id_2)) {
			return SPI_NAND_FLASH_RTN_NO_ERROR;
		}
	}

	return SPI_NAND_FLASH_RTN_PROBE_ERROR;
}

// Helper function to populate device info from a matched table entry
static void spi_nand_populate_device_info(
	struct SPI_NAND_FLASH_INFO_T *target_info,
	const struct SPI_NAND_FLASH_INFO_T *table_entry)
{
	int oob_size = OOB_size ? OOB_size : table_entry->oob_size;
	ecc_size = ((table_entry->device_size / table_entry->erase_size) *
				((table_entry->erase_size / table_entry->page_size) *
				 table_entry->oob_size));
	target_info->device_size = ECC_fcheck ? table_entry->device_size
									   : table_entry->device_size + ecc_size;
	erase_oob_size = (table_entry->erase_size / table_entry->page_size) *
					 table_entry->oob_size;
	target_info->erase_size = ECC_fcheck ? table_entry->erase_size
									  : table_entry->erase_size + erase_oob_size;
	target_info->page_size = ECC_fcheck ? table_entry->page_size
									 : table_entry->page_size + oob_size;
	target_info->oob_size = ECC_fcheck ? oob_size : 0;
	bmt_oob_size = table_entry->oob_size;
	target_info->dummy_mode = table_entry->dummy_mode;
	target_info->read_mode = table_entry->read_mode;
	target_info->write_mode = table_entry->write_mode;
	memcpy(&(target_info->ptr_name), &(table_entry->ptr_name),
		   sizeof(target_info->ptr_name));
	target_info->feature = table_entry->feature;
}


/* Probe SPI NAND flash ID */
static SPI_NAND_FLASH_RTN_T spi_nand_probe(struct SPI_NAND_FLASH_INFO_T *ptr_rtn_device_t) {
    u32 i = 0;
    size_t table_size = get_spi_nand_flash_table_size();
    SPI_NAND_FLASH_RTN_T rtn_status = SPI_NAND_FLASH_RTN_PROBE_ERROR;

    _SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "spi_nand_probe: start \n");

    /* Protocol for read id */
    _SPI_NAND_SEMAPHORE_LOCK();
    spi_nand_protocol_read_id(ptr_rtn_device_t);
    _SPI_NAND_SEMAPHORE_UNLOCK();

    for (i = 0; i < table_size; i++) {
        if (spi_nand_compare(ptr_rtn_device_t, &spi_nand_flash_tables[i]) == SPI_NAND_FLASH_RTN_NO_ERROR) {
            spi_nand_populate_device_info(ptr_rtn_device_t, &spi_nand_flash_tables[i]);
            rtn_status = SPI_NAND_FLASH_RTN_NO_ERROR;
            break;
        }
    }

    if (rtn_status != SPI_NAND_FLASH_RTN_NO_ERROR) {
        /* Another protocol for read id  (For example, the GigaDevice SPI NAND chip for Type C */
        _SPI_NAND_SEMAPHORE_LOCK();
        spi_nand_protocol_read_id_2(ptr_rtn_device_t);
        _SPI_NAND_SEMAPHORE_UNLOCK();

        for (i = 0; i < table_size; i++) {
            if (spi_nand_compare(ptr_rtn_device_t, &spi_nand_flash_tables[i]) == SPI_NAND_FLASH_RTN_NO_ERROR) {
                spi_nand_populate_device_info(ptr_rtn_device_t, &spi_nand_flash_tables[i]);
                rtn_status = SPI_NAND_FLASH_RTN_NO_ERROR;
                break;
            }
        }
    }

    if (rtn_status != SPI_NAND_FLASH_RTN_NO_ERROR) {
        /* Another protocol for read id (For example, the Toshiba/KIOXIA SPI NAND chip */
        _SPI_NAND_SEMAPHORE_LOCK();
        spi_nand_protocol_read_id_3(ptr_rtn_device_t);
        _SPI_NAND_SEMAPHORE_UNLOCK();

        for (i = 0; i < table_size; i++) {
            _SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1,
                                   "spi_nand_probe: table[%d]: mfr_id = 0x%x, dev_id = 0x%x\n", i,
                                   spi_nand_flash_tables[i].mfr_id, spi_nand_flash_tables[i].dev_id);

            if (((ptr_rtn_device_t->mfr_id) == spi_nand_flash_tables[i].mfr_id) &&
                ((ptr_rtn_device_t->dev_id) == spi_nand_flash_tables[i].dev_id)) {
                spi_nand_populate_device_info(ptr_rtn_device_t, &spi_nand_flash_tables[i]);
                rtn_status = SPI_NAND_FLASH_RTN_NO_ERROR;
                break;
            }
        }
    }

    if (ptr_rtn_device_t->dev_id_2 == 0) {
        _SPI_NAND_PRINTF("spi_nand_probe: mfr_id = 0x%x, dev_id = 0x%x\n", ptr_rtn_device_t->mfr_id,
                         ptr_rtn_device_t->dev_id);
    } else {
        _SPI_NAND_PRINTF("spi_nand_probe: mfr_id = 0x%x, dev_id = 0x%x, dev_id_2 = 0x%x\n",
                         ptr_rtn_device_t->mfr_id, ptr_rtn_device_t->dev_id, ptr_rtn_device_t->dev_id_2);
    }

    if (rtn_status == SPI_NAND_FLASH_RTN_NO_ERROR) {
        unsigned char feature = 0;
        _SPI_NAND_SEMAPHORE_LOCK();
        spi_nand_protocol_get_status_reg_1(&feature);
        _SPI_NAND_PRINTF("Get Status Register 1: 0x%02x\n", feature);
        spi_nand_protocol_get_status_reg_2(&feature);
        _SPI_NAND_PRINTF("Get Status Register 2: 0x%02x\n", feature);
        spi_nand_manufacturer_init(ptr_rtn_device_t);
        _SPI_NAND_SEMAPHORE_UNLOCK();
    }

    _SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "spi_nand_probe: end \n");

    return (rtn_status);
}

// SPI NAND Flash initialization function declaration.
SPI_NAND_FLASH_RTN_T SPI_NAND_Flash_Init(u32 rom_base)
{
	SPI_NAND_FLASH_RTN_T rtn_status = SPI_NAND_FLASH_RTN_PROBE_ERROR;

	/* 1. set SFC Clock to 50MHZ  */
	spi_nand_set_clock_speed(50);

	/* 2. Enable Manual Mode */
	_SPI_NAND_ENABLE_MANUAL_MODE();

	/* 3. Probe flash information */
	if ( spi_nand_probe(  &_current_flash_info_t) != SPI_NAND_FLASH_RTN_NO_ERROR )
	{
		fprintf(stderr, "SPI NAND Flash Not Detected!\n"); // Use stderr
	}
	else
	{

		if(ECC_fcheck) {
			_SPI_NAND_PRINTF("Using Flash ECC.\n");
		} else {
			_SPI_NAND_PRINTF("Disable Flash ECC.\n");
			if(OOB_size > bmt_oob_size) {
				fprintf(stderr, "Setting OOB size %dB cannot be larger %ldB!\n", OOB_size, bmt_oob_size); // Use stderr
				return SPI_NAND_FLASH_RTN_PROBE_ERROR;
			}
			if(OOB_size)
				_SPI_NAND_PRINTF("OOB Resize: %ldB to %dB.\n", bmt_oob_size, OOB_size);
		}
		SPI_NAND_Flash_Enable_OnDie_ECC();
		_SPI_NAND_PRINTF("Detected SPI NAND Flash: %s, Flash Size: %dMB, OOB Size: %ldB\n", _current_flash_info_t.ptr_name,  ECC_fcheck ? _current_flash_info_t.device_size >> 20 : (_current_flash_info_t.device_size - ecc_size) >> 20, OOB_size ? OOB_size : bmt_oob_size);

		rtn_status = SPI_NAND_FLASH_RTN_NO_ERROR;
	}

	return (rtn_status);
}

// Function to get the current flash information.
SPI_NAND_FLASH_RTN_T SPI_NAND_Flash_Get_Flash_Info( struct SPI_NAND_FLASH_INFO_T *ptr_rtn_into_t)
{
	struct SPI_NAND_FLASH_INFO_T *ptr_dev_info_t;
	SPI_NAND_FLASH_RTN_T rtn_status = SPI_NAND_FLASH_RTN_NO_ERROR;

	ptr_dev_info_t = _SPI_NAND_GET_DEVICE_INFO_PTR;

	memcpy( ptr_rtn_into_t, ptr_dev_info_t, sizeof(struct SPI_NAND_FLASH_INFO_T) );

	return (rtn_status);
}

SPI_NAND_FLASH_RTN_T SPI_NAND_Flash_Set_Flash_Info( struct SPI_NAND_FLASH_INFO_T *ptr_rtn_into_t)
{
	struct SPI_NAND_FLASH_INFO_T *ptr_dev_info_t;
	SPI_NAND_FLASH_RTN_T rtn_status = SPI_NAND_FLASH_RTN_NO_ERROR;

	ptr_dev_info_t = _SPI_NAND_GET_DEVICE_INFO_PTR;

	memcpy(ptr_dev_info_t, ptr_rtn_into_t, sizeof(struct SPI_NAND_FLASH_INFO_T) );

	return (rtn_status);
}

// Function to write N bytes into SPI NAND Flash.
SPI_NAND_FLASH_RTN_T SPI_NAND_Flash_Write_Nbyte( u32 dst_addr, u32 len, u32 *ptr_rtn_len, u8 *ptr_buf,
						SPI_NAND_FLASH_WRITE_SPEED_MODE_T speed_node )
{
	SPI_NAND_FLASH_RTN_T rtn_status = SPI_NAND_FLASH_RTN_NO_ERROR;

	rtn_status = spi_nand_write_internal(dst_addr, len, ptr_rtn_len, ptr_buf, speed_node);

	*ptr_rtn_len = len ;

	return (rtn_status);
}

// Function to read N bytes from SPI NAND Flash.
u32 SPI_NAND_Flash_Read_NByte(u32 addr, u32 len, u32 *retlen, u8 *buf, SPI_NAND_FLASH_READ_SPEED_MODE_T speed_mode, SPI_NAND_FLASH_RTN_T *status)
{
	SPI_NAND_FLASH_RTN_T rtn_status = spi_nand_read_internal(addr, len, buf, speed_mode, status);
	*retlen = len;
	return rtn_status;
}

// Function to erase SPI NAND Flash.
SPI_NAND_FLASH_RTN_T SPI_NAND_Flash_Erase(u32 dst_addr, u32 len) {
	SPI_NAND_FLASH_RTN_T rtn_status = SPI_NAND_FLASH_RTN_NO_ERROR;
	rtn_status = spi_nand_erase_internal(dst_addr, len);
	return rtn_status;
}

// Placeholder for SPI NAND Flash Read Byte function.
unsigned char SPI_NAND_Flash_Read_Byte(unsigned long addr, SPI_NAND_FLASH_RTN_T *status)
{
	u32 len = 1;
	u8 buf[2] = {0};
	struct SPI_NAND_FLASH_INFO_T *ptr_dev_info_t;
	ptr_dev_info_t = _SPI_NAND_GET_DEVICE_INFO_PTR;
	spi_nand_read_internal(addr, len, &buf[0], ptr_dev_info_t->read_mode, status);
	_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "SPI_NAND_Flash_Read_Byte : buf = 0x%x\n", buf[0]);
	return buf[0];
}

// Placeholder for SPI NAND Flash Read Double Word function.
unsigned long SPI_NAND_Flash_Read_DWord(unsigned long addr, SPI_NAND_FLASH_RTN_T *status)
{
	u8 buf2[4] = { 0 };
	u32 ret_val = 0;
	struct SPI_NAND_FLASH_INFO_T *ptr_dev_info_t;

	ptr_dev_info_t  = _SPI_NAND_GET_DEVICE_INFO_PTR;

	_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "SPI_NAND_Flash_Read_DWord, addr = 0x%llx\n", addr);

	spi_nand_read_internal(addr, 4, &buf2[0], ptr_dev_info_t->read_mode, status);
	ret_val = (buf2[0] << 24) | (buf2[1] << 16) | (buf2[2] <<8) | buf2[3];

	_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "SPI_NAND_Flash_Read_DWord : ret_val = 0x%x\n", ret_val);

	return ret_val;
}

// Clears the read cache data to ensure fresh data is read from the flash chip.
void SPI_NAND_Flash_Clear_Read_Cache_Data( void )
{
	_current_page_num = 0xFFFFFFFF;
}

SPI_NAND_FLASH_RTN_T SPI_NAND_Flash_Enable_OnDie_ECC( void )
{
	unsigned char feature;
	struct SPI_NAND_FLASH_INFO_T *ptr_dev_info_t;
	u8 die_num;
	int i;

	ptr_dev_info_t = _SPI_NAND_GET_DEVICE_INFO_PTR;

	if(((ptr_dev_info_t->feature) & SPI_NAND_FLASH_DIE_SELECT_1_HAVE)) {
		die_num = (ptr_dev_info_t->device_size / ptr_dev_info_t->page_size) >> 16;

		for(i = 0; i < die_num; i++) {
			spi_nand_protocol_die_select_1(i);

			spi_nand_protocol_get_status_reg_2(&feature);
			_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "before setting : SPI_NAND_Flash_Enable_OnDie_ECC, status reg = 0x%x\n", feature);
			if (ECC_fcheck)
				feature |= 0x10;
			else
				feature &= ~(1 << 4);
			spi_nand_protocol_set_status_reg_2(feature);

			/* Value check*/
			spi_nand_protocol_get_status_reg_2(&feature);
			_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "after setting : SPI_NAND_Flash_Enable_OnDie_ECC, status reg = 0x%x\n", feature);
		}
	} else if(((ptr_dev_info_t->feature) & SPI_NAND_FLASH_DIE_SELECT_2_HAVE)) {
		die_num = (ptr_dev_info_t->device_size / ptr_dev_info_t->page_size) >> 17;

		for(i = 0; i < die_num; i++) {
			spi_nand_protocol_die_select_2(i);

			spi_nand_protocol_get_status_reg_2(&feature);
			_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "before setting : SPI_NAND_Flash_Enable_OnDie_ECC, status reg = 0x%x\n", feature);
			if (ECC_fcheck)
				feature |= 0x10;
			else
				feature &= ~(1 << 4);
			spi_nand_protocol_set_status_reg_2(feature);

			/* Value check*/
			spi_nand_protocol_get_status_reg_2(&feature);
			_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "after setting : SPI_NAND_Flash_Enable_OnDie_ECC, status reg = 0x%x\n", feature);
		}
	} else {
		if( ((ptr_dev_info_t->mfr_id) == _SPI_NAND_MANUFACTURER_ID_PN) ||
			((ptr_dev_info_t->mfr_id) == _SPI_NAND_MANUFACTURER_ID_FM) ||
			((ptr_dev_info_t->mfr_id) == _SPI_NAND_MANUFACTURER_ID_FORESEE) ||
			(((ptr_dev_info_t->mfr_id) == _SPI_NAND_MANUFACTURER_ID_XTX) && ((ptr_dev_info_t->dev_id) == _SPI_NAND_DEVICE_ID_XT26G02B)) )
		{
			spi_nand_protocol_get_feature(_SPI_NAND_ADDR_ECC, &feature);
			_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "before setting : SPI_NAND_Flash_Enable_OnDie_ECC, ecc reg = 0x%x\n", feature);
			if (ECC_fcheck)
				feature |= 0x10;
			else
				feature &= ~(1 << 4);
			spi_nand_protocol_set_feature(_SPI_NAND_ADDR_ECC, feature);

			/* Value check*/
			spi_nand_protocol_get_feature(_SPI_NAND_ADDR_ECC, &feature);
			_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "after setting : SPI_NAND_Flash_Enable_OnDie_ECC, ecc reg = 0x%x\n", feature);
		}
		else
		{
			spi_nand_protocol_get_status_reg_2(&feature);
			_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "before setting : SPI_NAND_Flash_Enable_OnDie_ECC, status reg = 0x%x\n", feature);

			if (ECC_fcheck)
				feature |= 0x10;
			else
				feature &= ~(1 << 4);
			spi_nand_protocol_set_status_reg_2(feature);

			/* Value check*/
			spi_nand_protocol_get_status_reg_2(&feature);
			_SPI_NAND_DEBUG_PRINTF(SPI_NAND_FLASH_DEBUG_LEVEL_1, "after setting : SPI_NAND_Flash_Enable_OnDie_ECC, status reg = 0x%x\n", feature);
		}
	}

	if (ECC_fcheck)
		_ondie_ecc_flag = 1;
	else
		_ondie_ecc_flag = 0;
	return (SPI_NAND_FLASH_RTN_NO_ERROR);
}

int nandflash_init(int rom_base)
{
	if( SPI_NAND_Flash_Init(rom_base) == SPI_NAND_FLASH_RTN_NO_ERROR)
	{
		return 0;
	}
	else
	{
		return -1;
	}
}

int nandflash_read(unsigned long from, unsigned long len, unsigned long *retlen, unsigned char *buf, SPI_NAND_FLASH_RTN_T *status)
{
	struct SPI_NAND_FLASH_INFO_T *ptr_dev_info_t;

	ptr_dev_info_t  = _SPI_NAND_GET_DEVICE_INFO_PTR;

	timer_start();
	if (SPI_NAND_Flash_Read_NByte(from, len, (u32 *)retlen, buf, ptr_dev_info_t->read_mode, status) == SPI_NAND_FLASH_RTN_NO_ERROR) {
		timer_end();
		return 0;
	}  else {
		return -1;
	}
}

int nandflash_erase(unsigned long offset, unsigned long len)
{
	timer_start();
	if (SPI_NAND_Flash_Erase(offset, len) == SPI_NAND_FLASH_RTN_NO_ERROR) {
		timer_end();
		return 0;
	} else {
		return -1;
	}
}

int nandflash_write(unsigned long to, unsigned long len, unsigned long *retlen, unsigned char *buf)
{
	struct SPI_NAND_FLASH_INFO_T *ptr_dev_info_t;

	ptr_dev_info_t = _SPI_NAND_GET_DEVICE_INFO_PTR;

	timer_start();
	if (SPI_NAND_Flash_Write_Nbyte(to, len, (u32 *)retlen, buf, ptr_dev_info_t->write_mode) == SPI_NAND_FLASH_RTN_NO_ERROR) {
		timer_end();
		return 0;
	} else {
		return -1;
	}
}
/* End of [spi_nand_flash.c] package */

int snand_read(unsigned char *buf, unsigned long from, unsigned long len)
{
	unsigned long retlen = 0;
	SPI_NAND_FLASH_RTN_T status;

	if (!nandflash_read((unsigned long)from, (unsigned long)len, &retlen, buf, &status))
		return len;

	return -1;
}

int snand_erase(unsigned long offs, unsigned long len)
{
	return nandflash_erase((unsigned long)offs, (unsigned long)len);
}

int snand_write(unsigned char *buf, unsigned long to, unsigned long len)
{
	unsigned long retlen = 0;

	if (!nandflash_write((unsigned long)to, (unsigned long)len, &retlen, buf))
		return (int)retlen;

	return -1;
}

long snand_init(void)
{
	if (!nandflash_init(0)) {
		struct SPI_NAND_FLASH_INFO_T *ptr_dev_info_t = _SPI_NAND_GET_DEVICE_INFO_PTR;
		bsize = ptr_dev_info_t->erase_size;
		return (long)(ptr_dev_info_t->device_size);
	}

	return -1;
}

void support_snand_list(void)
{
	int i;
	size_t table_size = get_spi_nand_flash_table_size();

	_SPI_NAND_PRINTF("SPI NAND Flash Support List:\n");
	for (i = 0; i < table_size; i++) {
		_SPI_NAND_PRINTF("%03d. %s\n", i + 1, spi_nand_flash_tables[i].ptr_name);
	}
}
/* vim: set ts=8: */

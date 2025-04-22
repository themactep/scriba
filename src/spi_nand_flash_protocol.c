/*
 * spi_nand_flash_protocol.c
 * SPI NAND Flash protocol implementation.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "spi_nand_flash.h"
#include "spi_nand_flash_defs.h"

/* External variables */
extern unsigned char _plane_select_bit;
extern unsigned char _die_id;
extern struct SPI_NAND_FLASH_INFO_T _current_flash_info_t;

/* Set feature register */
SPI_NAND_FLASH_RTN_T spi_nand_protocol_set_feature(u8 addr, u8 data)
{
	SPI_CONTROLLER_RTN_T spi_ret;
	_SPI_NAND_READ_CHIP_SELECT_LOW(); // Assuming CS control doesn't fail easily
	spi_ret = _SPI_NAND_WRITE_ONE_BYTE(_SPI_NAND_OP_SET_FEATURE);
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;
	spi_ret = _SPI_NAND_WRITE_ONE_BYTE(addr);
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;
	spi_ret = _SPI_NAND_WRITE_ONE_BYTE(data);
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;
	_SPI_NAND_READ_CHIP_SELECT_HIGH();
	return SPI_NAND_FLASH_RTN_NO_ERROR;

spi_fail:
	_SPI_NAND_READ_CHIP_SELECT_HIGH();
	return SPI_NAND_FLASH_RTN_SPI_CTRL_FAIL;
}

/* Get feature register */
SPI_NAND_FLASH_RTN_T spi_nand_protocol_get_feature(u8 addr, u8 *ptr_rtn_data)
{
	SPI_CONTROLLER_RTN_T spi_ret;
	_SPI_NAND_READ_CHIP_SELECT_LOW();
	spi_ret = _SPI_NAND_WRITE_ONE_BYTE(_SPI_NAND_OP_GET_FEATURE);
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;
	spi_ret = _SPI_NAND_WRITE_ONE_BYTE(addr);
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;
	spi_ret = _SPI_NAND_READ_NBYTE(ptr_rtn_data, _SPI_NAND_LEN_ONE_BYTE, SPI_CONTROLLER_SPEED_SINGLE);
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;
	_SPI_NAND_READ_CHIP_SELECT_HIGH();
	return SPI_NAND_FLASH_RTN_NO_ERROR;

spi_fail:
	_SPI_NAND_READ_CHIP_SELECT_HIGH();
	return SPI_NAND_FLASH_RTN_SPI_CTRL_FAIL;
}

/* Status register 1 operations (protection) */
SPI_NAND_FLASH_RTN_T spi_nand_protocol_set_status_reg_1(u8 protection)
{
	return spi_nand_protocol_set_feature(_SPI_NAND_ADDR_PROTECTION, protection);
}

SPI_NAND_FLASH_RTN_T spi_nand_protocol_get_status_reg_1(u8 *ptr_rtn_protection)
{
	return spi_nand_protocol_get_feature(_SPI_NAND_ADDR_PROTECTION, ptr_rtn_protection);
}

/* Status register 2 operations (feature) */
SPI_NAND_FLASH_RTN_T spi_nand_protocol_set_status_reg_2(u8 feature)
{
	return spi_nand_protocol_set_feature(_SPI_NAND_ADDR_FEATURE, feature);
}

SPI_NAND_FLASH_RTN_T spi_nand_protocol_get_status_reg_2(u8 *ptr_rtn_feature)
{
	return spi_nand_protocol_get_feature(_SPI_NAND_ADDR_FEATURE, ptr_rtn_feature);
}

/* Status register 3 operations (status) */
SPI_NAND_FLASH_RTN_T spi_nand_protocol_get_status_reg_3(u8 *ptr_rtn_status)
{
	return spi_nand_protocol_get_feature(_SPI_NAND_ADDR_STATUS, ptr_rtn_status);
}

/* Status register 4 operations */
SPI_NAND_FLASH_RTN_T spi_nand_protocol_set_status_reg_4(u8 feature)
{
	return spi_nand_protocol_set_feature(_SPI_NAND_ADDR_FEATURE_4, feature);
}

SPI_NAND_FLASH_RTN_T spi_nand_protocol_get_status_reg_4(u8 *ptr_rtn_status)
{
	return spi_nand_protocol_get_feature(_SPI_NAND_ADDR_FEATURE_4, ptr_rtn_status);
}

/* Status register 5 operations */
SPI_NAND_FLASH_RTN_T spi_nand_protocol_get_status_reg_5(u8 *ptr_rtn_status)
{
	return spi_nand_protocol_get_feature(_SPI_NAND_ADDR_STATUS_5, ptr_rtn_status);
}

/* Write enable/disable */
SPI_NAND_FLASH_RTN_T spi_nand_protocol_write_enable(void)
{
	SPI_CONTROLLER_RTN_T spi_ret;
	_SPI_NAND_READ_CHIP_SELECT_LOW();
	spi_ret = _SPI_NAND_WRITE_ONE_BYTE(_SPI_NAND_OP_WRITE_ENABLE);
	_SPI_NAND_READ_CHIP_SELECT_HIGH();
	return (spi_ret == SPI_CONTROLLER_RTN_NO_ERROR) ? SPI_NAND_FLASH_RTN_NO_ERROR : SPI_NAND_FLASH_RTN_SPI_CTRL_FAIL;
}

SPI_NAND_FLASH_RTN_T spi_nand_protocol_write_disable(void)
{
	SPI_CONTROLLER_RTN_T spi_ret;
	_SPI_NAND_READ_CHIP_SELECT_LOW();
	spi_ret = _SPI_NAND_WRITE_ONE_BYTE(_SPI_NAND_OP_WRITE_DISABLE);
	_SPI_NAND_READ_CHIP_SELECT_HIGH();
	return (spi_ret == SPI_CONTROLLER_RTN_NO_ERROR) ? SPI_NAND_FLASH_RTN_NO_ERROR : SPI_NAND_FLASH_RTN_SPI_CTRL_FAIL;
}

/* Block erase */
SPI_NAND_FLASH_RTN_T spi_nand_protocol_block_erase(u32 block_idx)
{
	SPI_CONTROLLER_RTN_T spi_ret;
	_SPI_NAND_READ_CHIP_SELECT_LOW();
	spi_ret = _SPI_NAND_WRITE_ONE_BYTE(_SPI_NAND_OP_BLOCK_ERASE);
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;

	block_idx = block_idx << _SPI_NAND_BLOCK_ROW_ADDRESS_OFFSET;
	spi_ret = _SPI_NAND_WRITE_ONE_BYTE((block_idx >> 16) & 0xff);
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;
	spi_ret = _SPI_NAND_WRITE_ONE_BYTE((block_idx >> 8) & 0xff);
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;
	spi_ret = _SPI_NAND_WRITE_ONE_BYTE(block_idx & 0xff);
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;

	_SPI_NAND_READ_CHIP_SELECT_HIGH();
	return SPI_NAND_FLASH_RTN_NO_ERROR;

spi_fail:
	_SPI_NAND_READ_CHIP_SELECT_HIGH();
	return SPI_NAND_FLASH_RTN_SPI_CTRL_FAIL;
}

/* Read ID methods */
SPI_NAND_FLASH_RTN_T spi_nand_protocol_read_id(struct SPI_NAND_FLASH_INFO_T *ptr_rtn_flash_id)
{
	SPI_CONTROLLER_RTN_T spi_ret;
	_SPI_NAND_READ_CHIP_SELECT_LOW();
	spi_ret = _SPI_NAND_WRITE_ONE_BYTE(_SPI_NAND_OP_READ_ID);
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;
	spi_ret = _SPI_NAND_WRITE_ONE_BYTE(_SPI_NAND_ADDR_MANUFACTURE_ID);
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;

	spi_ret = _SPI_NAND_READ_NBYTE(&(ptr_rtn_flash_id->mfr_id), _SPI_NAND_LEN_ONE_BYTE, SPI_CONTROLLER_SPEED_SINGLE);
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;
	spi_ret = _SPI_NAND_READ_NBYTE(&(ptr_rtn_flash_id->dev_id), _SPI_NAND_LEN_ONE_BYTE, SPI_CONTROLLER_SPEED_SINGLE);
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;
	spi_ret = _SPI_NAND_READ_NBYTE(&(ptr_rtn_flash_id->dev_id_2), _SPI_NAND_LEN_ONE_BYTE, SPI_CONTROLLER_SPEED_SINGLE);
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;

	_SPI_NAND_READ_CHIP_SELECT_HIGH();
	return SPI_NAND_FLASH_RTN_NO_ERROR;

spi_fail:
	_SPI_NAND_READ_CHIP_SELECT_HIGH();
	return SPI_NAND_FLASH_RTN_SPI_CTRL_FAIL;
}

SPI_NAND_FLASH_RTN_T spi_nand_protocol_read_id_2(struct SPI_NAND_FLASH_INFO_T *ptr_rtn_flash_id)
{
	SPI_CONTROLLER_RTN_T spi_ret;
	_SPI_NAND_READ_CHIP_SELECT_LOW();
	spi_ret = _SPI_NAND_WRITE_ONE_BYTE(_SPI_NAND_OP_READ_ID);
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;

	spi_ret = _SPI_NAND_READ_NBYTE(&(ptr_rtn_flash_id->mfr_id), _SPI_NAND_LEN_ONE_BYTE, SPI_CONTROLLER_SPEED_SINGLE);
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;
	spi_ret = _SPI_NAND_READ_NBYTE(&(ptr_rtn_flash_id->dev_id), _SPI_NAND_LEN_ONE_BYTE, SPI_CONTROLLER_SPEED_SINGLE);
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;
	spi_ret = _SPI_NAND_READ_NBYTE(&(ptr_rtn_flash_id->dev_id_2), _SPI_NAND_LEN_ONE_BYTE, SPI_CONTROLLER_SPEED_SINGLE);
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;

	_SPI_NAND_READ_CHIP_SELECT_HIGH();
	return SPI_NAND_FLASH_RTN_NO_ERROR;

spi_fail:
	_SPI_NAND_READ_CHIP_SELECT_HIGH();
	return SPI_NAND_FLASH_RTN_SPI_CTRL_FAIL;
}

SPI_NAND_FLASH_RTN_T spi_nand_protocol_read_id_3(struct SPI_NAND_FLASH_INFO_T *ptr_rtn_flash_id)
{
	u8 dummy = 0;
	SPI_CONTROLLER_RTN_T spi_ret;

	_SPI_NAND_READ_CHIP_SELECT_LOW();
	spi_ret = _SPI_NAND_WRITE_ONE_BYTE(_SPI_NAND_OP_READ_ID);
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;

	spi_ret = _SPI_NAND_READ_NBYTE(&dummy, _SPI_NAND_LEN_ONE_BYTE, SPI_CONTROLLER_SPEED_SINGLE);
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;
	spi_ret = _SPI_NAND_READ_NBYTE(&(ptr_rtn_flash_id->mfr_id), _SPI_NAND_LEN_ONE_BYTE, SPI_CONTROLLER_SPEED_SINGLE);
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;
	spi_ret = _SPI_NAND_READ_NBYTE(&(ptr_rtn_flash_id->dev_id), _SPI_NAND_LEN_ONE_BYTE, SPI_CONTROLLER_SPEED_SINGLE);
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;

	_SPI_NAND_READ_CHIP_SELECT_HIGH();
	return SPI_NAND_FLASH_RTN_NO_ERROR;

spi_fail:
	_SPI_NAND_READ_CHIP_SELECT_HIGH();
	return SPI_NAND_FLASH_RTN_SPI_CTRL_FAIL;
}

/* Page read */
SPI_NAND_FLASH_RTN_T spi_nand_protocol_page_read(u32 page_number)
{
	u8 cmd[4];
	SPI_CONTROLLER_RTN_T spi_ret;

	_SPI_NAND_READ_CHIP_SELECT_LOW();

	cmd[0] = _SPI_NAND_OP_PAGE_READ;
	cmd[1] = (page_number >> 16) & 0xff;
	cmd[2] = (page_number >> 8) & 0xff;
	cmd[3] = (page_number) & 0xff;
	spi_ret = _SPI_NAND_WRITE_NBYTE(cmd, 4, SPI_CONTROLLER_SPEED_SINGLE);

	_SPI_NAND_READ_CHIP_SELECT_HIGH();
	return (spi_ret == SPI_CONTROLLER_RTN_NO_ERROR) ? SPI_NAND_FLASH_RTN_NO_ERROR : SPI_NAND_FLASH_RTN_SPI_CTRL_FAIL;
}

/* Read from cache */
SPI_NAND_FLASH_RTN_T spi_nand_protocol_read_from_cache(u32 data_offset, u32 len, u8 *ptr_rtn_buf, u32 read_mode,
						       SPI_NAND_FLASH_READ_DUMMY_BYTE_T dummy_mode)
{
	struct SPI_NAND_FLASH_INFO_T *ptr_dev_info_t = _SPI_NAND_GET_DEVICE_INFO_PTR;
	SPI_CONTROLLER_RTN_T spi_ret;
	u8 addr_high, addr_low;

	_SPI_NAND_READ_CHIP_SELECT_LOW();
	spi_ret = _SPI_NAND_WRITE_ONE_BYTE(_SPI_NAND_OP_READ_FROM_CACHE_SINGLE);
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;

	if (dummy_mode == SPI_NAND_FLASH_READ_DUMMY_BYTE_PREPEND)
	{
		spi_ret = _SPI_NAND_WRITE_ONE_BYTE(0xff); /* dummy byte */
		if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
			goto spi_fail;
	}

	if (((ptr_dev_info_t->feature) & SPI_NAND_FLASH_PLANE_SELECT_HAVE))
	{
		if (_plane_select_bit == 0)
		{
			addr_high = ((data_offset >> 8) & (0xef));
		}
		else
		{
			addr_high = ((data_offset >> 8) | (0x10));
		}
	}
	else
	{
		addr_high = ((data_offset >> 8) & (0xff));
	}
	addr_low = ((data_offset) & (0xff));

	spi_ret = _SPI_NAND_WRITE_ONE_BYTE(addr_high);
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;
	spi_ret = _SPI_NAND_WRITE_ONE_BYTE(addr_low);
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;

	if (dummy_mode == SPI_NAND_FLASH_READ_DUMMY_BYTE_APPEND)
	{
		spi_ret = _SPI_NAND_WRITE_ONE_BYTE(0xff); /* dummy byte */
		if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
			goto spi_fail;
	}

	if (dummy_mode == SPI_NAND_FLASH_READ_DUMMY_BYTE_PREPEND &&
	    ((read_mode == SPI_NAND_FLASH_READ_SPEED_MODE_DUAL) || (read_mode == SPI_NAND_FLASH_READ_SPEED_MODE_QUAD)))
	{
		spi_ret = _SPI_NAND_WRITE_ONE_BYTE(0xff); /* for dual/quad read dummy byte */
		if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
			goto spi_fail;
	}

	switch (read_mode)
	{
	case SPI_NAND_FLASH_READ_SPEED_MODE_SINGLE:
		spi_ret = _SPI_NAND_READ_NBYTE(ptr_rtn_buf, len, SPI_CONTROLLER_SPEED_SINGLE);
		break;
	case SPI_NAND_FLASH_READ_SPEED_MODE_DUAL:
		spi_ret = _SPI_NAND_READ_NBYTE(ptr_rtn_buf, len, SPI_CONTROLLER_SPEED_DUAL);
		break;
	case SPI_NAND_FLASH_READ_SPEED_MODE_QUAD:
		spi_ret = _SPI_NAND_READ_NBYTE(ptr_rtn_buf, len, SPI_CONTROLLER_SPEED_QUAD);
		break;
	default:
		spi_ret = SPI_CONTROLLER_RTN_NO_ERROR; // Or perhaps an error for invalid mode?
		break;
	}
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;

	_SPI_NAND_READ_CHIP_SELECT_HIGH();
	return SPI_NAND_FLASH_RTN_NO_ERROR;

spi_fail:
	_SPI_NAND_READ_CHIP_SELECT_HIGH();
	return SPI_NAND_FLASH_RTN_SPI_CTRL_FAIL;
}

/* Program load */
SPI_NAND_FLASH_RTN_T spi_nand_protocol_program_load(u32 addr, u8 *ptr_data, u32 len, u32 write_mode)
{
	struct SPI_NAND_FLASH_INFO_T *ptr_dev_info_t = _SPI_NAND_GET_DEVICE_INFO_PTR;
	SPI_CONTROLLER_RTN_T spi_ret;
	u8 addr_high, addr_low;

	_SPI_NAND_READ_CHIP_SELECT_LOW();
	spi_ret = _SPI_NAND_WRITE_ONE_BYTE(_SPI_NAND_OP_PROGRAM_LOAD_SINGLE);
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;

	if (((ptr_dev_info_t->feature) & SPI_NAND_FLASH_PLANE_SELECT_HAVE))
	{
		if (_plane_select_bit == 0)
		{
			addr_high = ((addr >> 8) & (0xef));
		}
		else
		{
			addr_high = ((addr >> 8) | (0x10));
		}
	}
	else
	{
		addr_high = ((addr >> 8) & (0xff));
	}
	addr_low = ((addr) & (0xff));

	spi_ret = _SPI_NAND_WRITE_ONE_BYTE(addr_high);
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;
	spi_ret = _SPI_NAND_WRITE_ONE_BYTE(addr_low);
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;

	switch (write_mode)
	{
	case SPI_NAND_FLASH_WRITE_SPEED_MODE_SINGLE:
		spi_ret = _SPI_NAND_WRITE_NBYTE(ptr_data, len, SPI_CONTROLLER_SPEED_SINGLE);
		break;
	case SPI_NAND_FLASH_WRITE_SPEED_MODE_QUAD:
		spi_ret = _SPI_NAND_WRITE_NBYTE(ptr_data, len, SPI_CONTROLLER_SPEED_QUAD);
		break;
	default:
		spi_ret = SPI_CONTROLLER_RTN_NO_ERROR; // Or perhaps an error for invalid mode?
		break;
	}
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;

	_SPI_NAND_READ_CHIP_SELECT_HIGH();
	return SPI_NAND_FLASH_RTN_NO_ERROR;

spi_fail:
	_SPI_NAND_READ_CHIP_SELECT_HIGH();
	return SPI_NAND_FLASH_RTN_SPI_CTRL_FAIL;
}

/* Program execute */
SPI_NAND_FLASH_RTN_T spi_nand_protocol_program_execute(u32 addr)
{
	SPI_CONTROLLER_RTN_T spi_ret;
	_SPI_NAND_READ_CHIP_SELECT_LOW();
	spi_ret = _SPI_NAND_WRITE_ONE_BYTE(_SPI_NAND_OP_PROGRAM_EXECUTE);
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;

	spi_ret = _SPI_NAND_WRITE_ONE_BYTE(((addr >> 16) & 0xff));
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;
	spi_ret = _SPI_NAND_WRITE_ONE_BYTE(((addr >> 8) & 0xff));
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;
	spi_ret = _SPI_NAND_WRITE_ONE_BYTE(((addr) & 0xff));
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;

	_SPI_NAND_READ_CHIP_SELECT_HIGH();
	return SPI_NAND_FLASH_RTN_NO_ERROR;

spi_fail:
	_SPI_NAND_READ_CHIP_SELECT_HIGH();
	return SPI_NAND_FLASH_RTN_SPI_CTRL_FAIL;
}

/* Die select methods */
SPI_NAND_FLASH_RTN_T spi_nand_protocol_die_select_1(u8 die_id)
{
	SPI_CONTROLLER_RTN_T spi_ret;
	_SPI_NAND_READ_CHIP_SELECT_LOW();
	spi_ret = _SPI_NAND_WRITE_ONE_BYTE(_SPI_NAND_OP_DIE_SELECT);
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;
	spi_ret = _SPI_NAND_WRITE_ONE_BYTE(die_id);
	if (spi_ret != SPI_CONTROLLER_RTN_NO_ERROR)
		goto spi_fail;
	_SPI_NAND_READ_CHIP_SELECT_HIGH();
	return SPI_NAND_FLASH_RTN_NO_ERROR;

spi_fail:
	_SPI_NAND_READ_CHIP_SELECT_HIGH();
	return SPI_NAND_FLASH_RTN_SPI_CTRL_FAIL;
}

SPI_NAND_FLASH_RTN_T spi_nand_protocol_die_select_2(u8 die_id)
{
	u8 feature;
	SPI_NAND_FLASH_RTN_T rtn_status;

	rtn_status = spi_nand_protocol_get_status_reg_4(&feature);
	if (rtn_status != SPI_NAND_FLASH_RTN_NO_ERROR)
	{
		return rtn_status;
	}

	if (die_id == 0)
	{
		feature &= ~(_SPI_NAND_VAL_DIE_SELECT_BIT_MICRON); // Use defined constant
	}
	else
	{
		feature |= _SPI_NAND_VAL_DIE_SELECT_BIT_MICRON; // Use defined constant
	}

	return spi_nand_protocol_set_status_reg_4(feature);
}

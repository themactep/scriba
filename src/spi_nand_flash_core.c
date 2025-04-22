/*
 * spi_nand_flash_core.c
 * SPI NAND Flash core functionality implementation.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "spi_nand_flash_defs.h"

/* Global variables */
struct SPI_NAND_FLASH_INFO_T _current_flash_info_t;
unsigned char _plane_select_bit;
unsigned char _die_id;

/* External references to protocol functions */
extern SPI_NAND_FLASH_RTN_T spi_nand_protocol_set_feature(u8 addr, u8 data);
extern SPI_NAND_FLASH_RTN_T spi_nand_protocol_get_feature(u8 addr, u8 *ptr_rtn_data);
extern SPI_NAND_FLASH_RTN_T spi_nand_protocol_set_status_reg_1(u8 protection);
extern SPI_NAND_FLASH_RTN_T spi_nand_protocol_get_status_reg_1(u8 *ptr_rtn_protection);
extern SPI_NAND_FLASH_RTN_T spi_nand_protocol_set_status_reg_2(u8 feature);
extern SPI_NAND_FLASH_RTN_T spi_nand_protocol_get_status_reg_2(u8 *ptr_rtn_feature);
extern SPI_NAND_FLASH_RTN_T spi_nand_protocol_get_status_reg_3(u8 *ptr_rtn_status);
extern SPI_NAND_FLASH_RTN_T spi_nand_protocol_write_enable(void);
extern SPI_NAND_FLASH_RTN_T spi_nand_protocol_write_disable(void);
extern SPI_NAND_FLASH_RTN_T spi_nand_protocol_block_erase(u32 block_idx);
extern SPI_NAND_FLASH_RTN_T spi_nand_protocol_read_id(struct SPI_NAND_FLASH_INFO_T *ptr_rtn_flash_id);
extern SPI_NAND_FLASH_RTN_T spi_nand_protocol_read_id_2(struct SPI_NAND_FLASH_INFO_T *ptr_rtn_flash_id);
extern SPI_NAND_FLASH_RTN_T spi_nand_protocol_read_id_3(struct SPI_NAND_FLASH_INFO_T *ptr_rtn_flash_id);
extern SPI_NAND_FLASH_RTN_T spi_nand_protocol_page_read(u32 page_number);
extern SPI_NAND_FLASH_RTN_T spi_nand_protocol_read_from_cache(u32 data_offset, u32 len, u8 *ptr_rtn_buf, u32 read_mode, SPI_NAND_FLASH_READ_DUMMY_BYTE_T dummy_mode);
extern SPI_NAND_FLASH_RTN_T spi_nand_protocol_program_load(u32 addr, u8 *ptr_data, u32 len, u32 write_mode);
extern SPI_NAND_FLASH_RTN_T spi_nand_protocol_program_execute(u32 addr);
extern SPI_NAND_FLASH_RTN_T spi_nand_protocol_die_select_1(u8 die_id);
extern SPI_NAND_FLASH_RTN_T spi_nand_protocol_die_select_2(u8 die_id);

/* External reference to flash tables */
extern const struct SPI_NAND_FLASH_INFO_T spi_nand_flash_tables[];

/*
 * Wait for device to be ready
 */
SPI_NAND_FLASH_RTN_T spi_nand_check_status(void)
{
	SPI_NAND_FLASH_RTN_T rtn_status = SPI_NAND_FLASH_RTN_NO_ERROR;
	u8 status;
	u32 timeout = 0;

	do {
		rtn_status = spi_nand_protocol_get_status_reg_3(&status);
		if (rtn_status != SPI_NAND_FLASH_RTN_NO_ERROR) {
			return rtn_status;
		}

		if (timeout++ > SPI_NAND_FLASH_TIMEOUT) {
			return SPI_NAND_FLASH_RTN_TIMEOUT;
		}
	} while ((status & _SPI_NAND_VAL_OIP) == _SPI_NAND_VAL_OIP);

	if ((status & _SPI_NAND_VAL_ERASE_FAIL) == _SPI_NAND_VAL_ERASE_FAIL) {
		return SPI_NAND_FLASH_RTN_ERASE_FAIL;
	}

	if ((status & _SPI_NAND_VAL_PROGRAM_FAIL) == _SPI_NAND_VAL_PROGRAM_FAIL) {
		return SPI_NAND_FLASH_RTN_PROGRAM_FAIL;
	}

	return SPI_NAND_FLASH_RTN_NO_ERROR;
}

/*
 * Detect SPI NAND flash device
 */
SPI_NAND_FLASH_RTN_T spi_nand_flash_detect_device(struct SPI_NAND_FLASH_INFO_T *ptr_rtn_device_t)
{
	SPI_NAND_FLASH_RTN_T rtn_status = SPI_NAND_FLASH_RTN_NO_ERROR;
	struct SPI_NAND_FLASH_INFO_T flash_id;
	int i;

	/* Try different read ID methods */
	rtn_status = spi_nand_protocol_read_id(&flash_id);
	if (rtn_status != SPI_NAND_FLASH_RTN_NO_ERROR) {
		return rtn_status;
	}

	if ((flash_id.mfr_id == 0) || (flash_id.mfr_id == 0xFF)) {
		rtn_status = spi_nand_protocol_read_id_2(&flash_id);
		if (rtn_status != SPI_NAND_FLASH_RTN_NO_ERROR) {
			return rtn_status;
		}
	}

	if ((flash_id.mfr_id == 0) || (flash_id.mfr_id == 0xFF)) {
		rtn_status = spi_nand_protocol_read_id_3(&flash_id);
		if (rtn_status != SPI_NAND_FLASH_RTN_NO_ERROR) {
			return rtn_status;
		}
	}

	/* Search for matching device in tables */
	for (i = 0; spi_nand_flash_tables[i].ptr_name != NULL; i++) {
		if (flash_id.mfr_id == spi_nand_flash_tables[i].mfr_id) {
			if ((flash_id.dev_id == spi_nand_flash_tables[i].dev_id) &&
			    ((flash_id.dev_id_2 == spi_nand_flash_tables[i].dev_id_2) || (spi_nand_flash_tables[i].dev_id_2 == 0))) {
				*ptr_rtn_device_t = spi_nand_flash_tables[i];
				return SPI_NAND_FLASH_RTN_NO_ERROR;
			}
		}
	}

	return SPI_NAND_FLASH_RTN_DEVICE_DETECTION_ERROR;
}

/*
 * Initialize SPI NAND flash
 */
SPI_NAND_FLASH_RTN_T spi_nand_flash_init(struct SPI_NAND_FLASH_INFO_T *ptr_device_t)
{
	SPI_NAND_FLASH_RTN_T rtn_status = SPI_NAND_FLASH_RTN_NO_ERROR;
	u8 feature;

	/* Initialize global variables */
	_plane_select_bit = 0;
	_die_id = 0;
	_current_flash_info_t = *ptr_device_t;

	/* Disable block protection */
	rtn_status = spi_nand_protocol_set_status_reg_1(0);
	if (rtn_status != SPI_NAND_FLASH_RTN_NO_ERROR) {
		return rtn_status;
	}

	/* Enable ECC */
	rtn_status = spi_nand_protocol_get_status_reg_2(&feature);
	if (rtn_status != SPI_NAND_FLASH_RTN_NO_ERROR) {
		return rtn_status;
	}

	feature |= _SPI_NAND_VAL_ECC_ENABLE;
	rtn_status = spi_nand_protocol_set_status_reg_2(feature);
	if (rtn_status != SPI_NAND_FLASH_RTN_NO_ERROR) {
		return rtn_status;
	}

	return SPI_NAND_FLASH_RTN_NO_ERROR;
}

/*
 * Read page data from SPI NAND flash
 */
SPI_NAND_FLASH_RTN_T spi_nand_flash_read_page(u32 page_number, u32 column_number, u32 len, u8 *ptr_rtn_buf)
{
	SPI_NAND_FLASH_RTN_T rtn_status = SPI_NAND_FLASH_RTN_NO_ERROR;
	struct SPI_NAND_FLASH_INFO_T *ptr_dev_info_t = &_current_flash_info_t;

	/* Handle die select if needed */
	if ((ptr_dev_info_t->feature & SPI_NAND_FLASH_DIE_SELECT_1_HAVE) &&
	    (page_number >= (ptr_dev_info_t->device_size / 2) / ptr_dev_info_t->erase_size * 64)) {
		if (_die_id != 1) {
			_die_id = 1;
			rtn_status = spi_nand_protocol_die_select_1(_die_id);
			if (rtn_status != SPI_NAND_FLASH_RTN_NO_ERROR) {
				return rtn_status;
			}
		}
		page_number -= (ptr_dev_info_t->device_size / 2) / ptr_dev_info_t->erase_size * 64;
	} else {
		if (_die_id != 0) {
			_die_id = 0;
			rtn_status = spi_nand_protocol_die_select_1(_die_id);
			if (rtn_status != SPI_NAND_FLASH_RTN_NO_ERROR) {
				return rtn_status;
			}
		}
	}

	/* Handle plane select if needed */
	if ((ptr_dev_info_t->feature & SPI_NAND_FLASH_PLANE_SELECT_HAVE) && (page_number % 2)) {
		_plane_select_bit = 1;
	} else {
		_plane_select_bit = 0;
	}

	/* Load page into cache */
	rtn_status = spi_nand_protocol_page_read(page_number);
	if (rtn_status != SPI_NAND_FLASH_RTN_NO_ERROR) {
		return rtn_status;
	}

	/* Wait for operation to complete */
	rtn_status = spi_nand_check_status();
	if (rtn_status != SPI_NAND_FLASH_RTN_NO_ERROR) {
		return rtn_status;
	}

	/* Read data from cache */
	rtn_status = spi_nand_protocol_read_from_cache(column_number, len, ptr_rtn_buf,
	                                              ptr_dev_info_t->read_mode,
	                                              ptr_dev_info_t->dummy_mode);
	if (rtn_status != SPI_NAND_FLASH_RTN_NO_ERROR) {
		return rtn_status;
	}

	return SPI_NAND_FLASH_RTN_NO_ERROR;
}

/*
 * Write page data to SPI NAND flash
 */
SPI_NAND_FLASH_RTN_T spi_nand_flash_write_page(u32 page_number, u32 column_number, u32 len, u8 *ptr_buf)
{
	SPI_NAND_FLASH_RTN_T rtn_status = SPI_NAND_FLASH_RTN_NO_ERROR;
	struct SPI_NAND_FLASH_INFO_T *ptr_dev_info_t = &_current_flash_info_t;

	/* Handle die select if needed */
	if ((ptr_dev_info_t->feature & SPI_NAND_FLASH_DIE_SELECT_1_HAVE) &&
	    (page_number >= (ptr_dev_info_t->device_size / 2) / ptr_dev_info_t->erase_size * 64)) {
		if (_die_id != 1) {
			_die_id = 1;
			rtn_status = spi_nand_protocol_die_select_1(_die_id);
			if (rtn_status != SPI_NAND_FLASH_RTN_NO_ERROR) {
				return rtn_status;
			}
		}
		page_number -= (ptr_dev_info_t->device_size / 2) / ptr_dev_info_t->erase_size * 64;
	} else {
		if (_die_id != 0) {
			_die_id = 0;
			rtn_status = spi_nand_protocol_die_select_1(_die_id);
			if (rtn_status != SPI_NAND_FLASH_RTN_NO_ERROR) {
				return rtn_status;
			}
		}
	}

	/* Handle plane select if needed */
	if ((ptr_dev_info_t->feature & SPI_NAND_FLASH_PLANE_SELECT_HAVE) && (page_number % 2)) {
		_plane_select_bit = 1;
	} else {
		_plane_select_bit = 0;
	}

	/* Enable write */
	rtn_status = spi_nand_protocol_write_enable();
	if (rtn_status != SPI_NAND_FLASH_RTN_NO_ERROR) {
		return rtn_status;
	}

	/* Load data to cache */
	rtn_status = spi_nand_protocol_program_load(column_number, ptr_buf, len, ptr_dev_info_t->write_mode);
	if (rtn_status != SPI_NAND_FLASH_RTN_NO_ERROR) {
		return rtn_status;
	}

	/* Execute program */
	rtn_status = spi_nand_protocol_program_execute(page_number);
	if (rtn_status != SPI_NAND_FLASH_RTN_NO_ERROR) {
		return rtn_status;
	}

	/* Wait for operation to complete */
	rtn_status = spi_nand_check_status();
	if (rtn_status != SPI_NAND_FLASH_RTN_NO_ERROR) {
		return rtn_status;
	}

	return SPI_NAND_FLASH_RTN_NO_ERROR;
}

/*
 * Erase a block in SPI NAND flash
 */
SPI_NAND_FLASH_RTN_T spi_nand_flash_erase_block(u32 block_idx)
{
	SPI_NAND_FLASH_RTN_T rtn_status = SPI_NAND_FLASH_RTN_NO_ERROR;
	struct SPI_NAND_FLASH_INFO_T *ptr_dev_info_t = &_current_flash_info_t;
	u32 page_number;

	/* Calculate page number from block index */
	page_number = block_idx * (ptr_dev_info_t->erase_size / ptr_dev_info_t->page_size);

	/* Handle die select if needed */
	if ((ptr_dev_info_t->feature & SPI_NAND_FLASH_DIE_SELECT_1_HAVE) &&
	    (page_number >= (ptr_dev_info_t->device_size / 2) / ptr_dev_info_t->erase_size * 64)) {
		if (_die_id != 1) {
			_die_id = 1;
			rtn_status = spi_nand_protocol_die_select_1(_die_id);
			if (rtn_status != SPI_NAND_FLASH_RTN_NO_ERROR) {
				return rtn_status;
			}
		}
		page_number -= (ptr_dev_info_t->device_size / 2) / ptr_dev_info_t->erase_size * 64;
		block_idx = page_number / (ptr_dev_info_t->erase_size / ptr_dev_info_t->page_size);
	} else {
		if (_die_id != 0) {
			_die_id = 0;
			rtn_status = spi_nand_protocol_die_select_1(_die_id);
			if (rtn_status != SPI_NAND_FLASH_RTN_NO_ERROR) {
				return rtn_status;
			}
		}
	}

	/* Enable write */
	rtn_status = spi_nand_protocol_write_enable();
	if (rtn_status != SPI_NAND_FLASH_RTN_NO_ERROR) {
		return rtn_status;
	}

	/* Erase block */
	rtn_status = spi_nand_protocol_block_erase(block_idx);
	if (rtn_status != SPI_NAND_FLASH_RTN_NO_ERROR) {
		return rtn_status;
	}

	/* Wait for operation to complete */
	rtn_status = spi_nand_check_status();
	if (rtn_status != SPI_NAND_FLASH_RTN_NO_ERROR) {
		return rtn_status;
	}

	return SPI_NAND_FLASH_RTN_NO_ERROR;
}

/*
 * Get SPI NAND flash device information
 */
SPI_NAND_FLASH_RTN_T spi_nand_flash_get_info(struct SPI_NAND_FLASH_INFO_T *ptr_rtn_info_t)
{
	*ptr_rtn_info_t = _current_flash_info_t;
	return SPI_NAND_FLASH_RTN_NO_ERROR;
}

/*
 * Get ECC status from last read operation
 */
SPI_NAND_FLASH_RTN_T spi_nand_flash_get_ecc_status(u8 *ptr_rtn_ecc_status)
{
	SPI_NAND_FLASH_RTN_T rtn_status = SPI_NAND_FLASH_RTN_NO_ERROR;
	u8 status;

	rtn_status = spi_nand_protocol_get_status_reg_3(&status);
	if (rtn_status != SPI_NAND_FLASH_RTN_NO_ERROR) {
		return rtn_status;
	}

	*ptr_rtn_ecc_status = (status & _SPI_NAND_VAL_ECC_STATUS_MASK) >> _SPI_NAND_VAL_ECC_STATUS_OFFSET;
	return SPI_NAND_FLASH_RTN_NO_ERROR;
}

/*
 * Check if block is bad
 */
SPI_NAND_FLASH_RTN_T spi_nand_flash_is_bad_block(u32 block_idx, u8 *ptr_rtn_is_bad)
{
	SPI_NAND_FLASH_RTN_T rtn_status = SPI_NAND_FLASH_RTN_NO_ERROR;
	struct SPI_NAND_FLASH_INFO_T *ptr_dev_info_t = &_current_flash_info_t;
	u32 page_number;
	u8 spare_data;

	/* Calculate page number from block index */
	page_number = block_idx * (ptr_dev_info_t->erase_size / ptr_dev_info_t->page_size);

	/* Read bad block marker from spare area */
	rtn_status = spi_nand_flash_read_page(page_number, ptr_dev_info_t->page_size, 1, &spare_data);
	if (rtn_status != SPI_NAND_FLASH_RTN_NO_ERROR) {
		return rtn_status;
	}

	*ptr_rtn_is_bad = (spare_data != 0xFF);
	return SPI_NAND_FLASH_RTN_NO_ERROR;
}

/*
 * Mark block as bad
 */
SPI_NAND_FLASH_RTN_T spi_nand_flash_mark_bad_block(u32 block_idx)
{
	SPI_NAND_FLASH_RTN_T rtn_status = SPI_NAND_FLASH_RTN_NO_ERROR;
	struct SPI_NAND_FLASH_INFO_T *ptr_dev_info_t = &_current_flash_info_t;
	u32 page_number;
	u8 bad_mark = 0;

	/* Calculate page number from block index */
	page_number = block_idx * (ptr_dev_info_t->erase_size / ptr_dev_info_t->page_size);

	/* Write bad block marker to spare area */
	rtn_status = spi_nand_flash_write_page(page_number, ptr_dev_info_t->page_size, 1, &bad_mark);
	if (rtn_status != SPI_NAND_FLASH_RTN_NO_ERROR) {
		return rtn_status;
	}

	return SPI_NAND_FLASH_RTN_NO_ERROR;
}

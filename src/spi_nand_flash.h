/*
 * Copyright (C) 2018-2021 McMCC <mcmcc@mail.ru>
 * spi_nand_flash.h - SPI NAND Flash access interface.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef __SPI_NAND_FLASH_H__
#define __SPI_NAND_FLASH_H__

#include "types.h"
#include "ch341a_spi.h"

#define SPI_NAND_FLASH_OOB_FREE_ENTRY_MAX	32

// Enum for specifying dummy byte placement in read operations.
typedef enum {
	SPI_NAND_FLASH_READ_DUMMY_BYTE_PREPEND,
	SPI_NAND_FLASH_READ_DUMMY_BYTE_APPEND,
	SPI_NAND_FLASH_READ_DUMMY_BYTE_DEF_NO
} SPI_NAND_FLASH_READ_DUMMY_BYTE_T;

// Return codes for SPI NAND operations.
typedef enum {
	SPI_NAND_FLASH_RTN_NO_ERROR = 0,
	SPI_NAND_FLASH_RTN_PROBE_ERROR,
	SPI_NAND_FLASH_RTN_ALIGNED_CHECK_FAIL,
	SPI_NAND_FLASH_RTN_DETECTED_BAD_BLOCK,
	SPI_NAND_FLASH_RTN_ERASE_FAIL,
	SPI_NAND_FLASH_RTN_PROGRAM_FAIL,
	SPI_NAND_FLASH_RTN_SPI_CTRL_FAIL, // Added for SPI controller communication errors
	SPI_NAND_FLASH_RTN_DEF_NO
} SPI_NAND_FLASH_RTN_T;

// SPI NAND read speed modes.
typedef enum {
	SPI_NAND_FLASH_READ_SPEED_MODE_SINGLE = 0,
	SPI_NAND_FLASH_READ_SPEED_MODE_DUAL,
	SPI_NAND_FLASH_READ_SPEED_MODE_QUAD,
	SPI_NAND_FLASH_READ_SPEED_MODE_DEF_NO
} SPI_NAND_FLASH_READ_SPEED_MODE_T;

// SPI NAND write speed modes.
typedef enum {
	SPI_NAND_FLASH_WRITE_SPEED_MODE_SINGLE = 0,
	SPI_NAND_FLASH_WRITE_SPEED_MODE_QUAD,
	SPI_NAND_FLASH_WRITE_SPEED_MODE_DEF_NO
} SPI_NAND_FLASH_WRITE_SPEED_MODE_T;

// Debug levels for SPI NAND operations.
typedef enum {
	SPI_NAND_FLASH_DEBUG_LEVEL_0 = 0,
	SPI_NAND_FLASH_DEBUG_LEVEL_1,
	SPI_NAND_FLASH_DEBUG_LEVEL_2,
	SPI_NAND_FLASH_DEBUG_LEVEL_DEF_NO
} SPI_NAND_FLASH_DEBUG_LEVEL_T;

/* Bitwise feature flags */
#define SPI_NAND_FLASH_FEATURE_NONE		( 0x00 )
#define SPI_NAND_FLASH_PLANE_SELECT_HAVE	( 0x01 << 0 )
#define SPI_NAND_FLASH_DIE_SELECT_1_HAVE	( 0x01 << 1 )
#define SPI_NAND_FLASH_DIE_SELECT_2_HAVE	( 0x01 << 2 )

// Structure holding information about a specific SPI NAND flash chip.
struct SPI_NAND_FLASH_INFO_T {
	u8					mfr_id;
	u8					dev_id;
	u8					dev_id_2;
	const char				*ptr_name;
	u32					device_size;	/* Flash total Size */
	u32					page_size;	/* Page Size */
	u32					erase_size;	/* Block Size */
	u32					oob_size;	/* Spare Area (OOB) Size */
	SPI_NAND_FLASH_READ_DUMMY_BYTE_T	dummy_mode;
	SPI_NAND_FLASH_READ_SPEED_MODE_T	read_mode;
	SPI_NAND_FLASH_WRITE_SPEED_MODE_T	write_mode;
	u32					feature;
};

// Legacy NAND info structure.
struct nand_info {
	int mfr_id;
	int dev_id;
	char *name;
	int numchips;
	int chip_shift;
	int page_shift;
	int erase_shift;
	int oob_shift;
	int badblockpos;
	int opcode_type;
};

// Legacy NAND chip structure.
struct ra_nand_chip {
	struct nand_info *flash;
};

/**
 * @brief Initialize the SPI NAND flash interface.
 * @param rom_base Base address (unused).
 * @return SPI_NAND_FLASH_RTN_NO_ERROR on success, error code otherwise.
 */
SPI_NAND_FLASH_RTN_T SPI_NAND_Flash_Init( u32   rom_base );

/**
 * @brief Get information about the detected SPI NAND flash chip.
 * @param ptr_rtn_into_t Pointer to a structure to store the flash info.
 * @return SPI_NAND_FLASH_RTN_NO_ERROR on success.
 */
SPI_NAND_FLASH_RTN_T SPI_NAND_Flash_Get_Flash_Info( struct SPI_NAND_FLASH_INFO_T *ptr_rtn_into_t);

/**
 * @brief Write N bytes to the SPI NAND flash.
 * @param dst_addr Destination address.
 * @param len Number of bytes to write.
 * @param ptr_rtn_len Pointer to store the actual number of bytes written.
 * @param ptr_buf Pointer to the data buffer.
 * @param speed_mode Write speed mode.
 * @return SPI_NAND_FLASH_RTN_NO_ERROR on success, error code otherwise.
 */
SPI_NAND_FLASH_RTN_T SPI_NAND_Flash_Write_Nbyte( u32					dst_addr,
                                                 u32					len,
                                                 u32					*ptr_rtn_len,
                                                 u8					*ptr_buf,
                                                 SPI_NAND_FLASH_WRITE_SPEED_MODE_T	speed_mode );

/**
 * @brief Read N bytes from the SPI NAND flash.
 * @param addr Source address.
 * @param len Number of bytes to read.
 * @param retlen Pointer to store the actual number of bytes read.
 * @param buf Pointer to the destination buffer.
 * @param speed_mode Read speed mode.
 * @param status Pointer to store the operation status (e.g., bad block detection).
 * @return Number of bytes read on success, error code otherwise.
 */
u32 SPI_NAND_Flash_Read_NByte( u32					addr,
                               u32					len,
                               u32					*retlen,
                               u8					*buf,
                               SPI_NAND_FLASH_READ_SPEED_MODE_T		speed_mode,
                               SPI_NAND_FLASH_RTN_T			*status );

/**
 * @brief Erase a region of the SPI NAND flash.
 * @param dst_addr Start address of the region to erase (must be block-aligned).
 * @param len Length of the region to erase (must be block-aligned).
 * @return SPI_NAND_FLASH_RTN_NO_ERROR on success, error code otherwise.
 */
SPI_NAND_FLASH_RTN_T SPI_NAND_Flash_Erase( u32  dst_addr,
                                           u32  len      );

/**
 * @brief Read a single byte from the SPI NAND flash.
 * @param addr Source address.
 * @param status Pointer to store the operation status.
 * @return The byte read.
 */
unsigned char SPI_NAND_Flash_Read_Byte( unsigned long    addr, SPI_NAND_FLASH_RTN_T *status);

/**
 * @brief Read a double word (32 bits) from the SPI NAND flash.
 * @param addr Source address.
 * @param status Pointer to store the operation status.
 * @return The double word read.
 */
unsigned long SPI_NAND_Flash_Read_DWord( unsigned long  addr, SPI_NAND_FLASH_RTN_T *status);

/**
 * @brief Clear the internal read cache. Forces next read to fetch from flash.
 */
void SPI_NAND_Flash_Clear_Read_Cache_Data( void );

/**
 * @brief Enable or disable the on-die ECC feature based on global ECC_fcheck flag.
 * @return SPI_NAND_FLASH_RTN_NO_ERROR on success.
 */
SPI_NAND_FLASH_RTN_T SPI_NAND_Flash_Enable_OnDie_ECC( void );

/**
 * @brief Erase a single block of the SPI NAND flash.
 * @param block_index Index of the block to erase.
 * @return SPI_NAND_FLASH_RTN_NO_ERROR on success, error code otherwise.
 */
SPI_NAND_FLASH_RTN_T spi_nand_erase_block ( u32 block_index);

#endif /* ifndef __SPI_NAND_FLASH_H__ */

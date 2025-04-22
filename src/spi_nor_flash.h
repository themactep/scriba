#ifndef SPI_NOR_FLASH_H
#define SPI_NOR_FLASH_H

#include "types.h"
#include <stdint.h>
#include <stdbool.h>

#define min(a, b) (((a) < (b)) ? (a) : (b))

/******************************************************************************
 * SPI FLASH elementary definitions
 ******************************************************************************/

#define FLASH_PAGESIZE 256

/* Flash opcodes. */
#define OPCODE_WREN 6	 /* Write enable */
#define OPCODE_WRDI 4	 /* Write disable */
#define OPCODE_RDSR 5	 /* Read status register */
#define OPCODE_WRSR 1	 /* Write status register */
#define OPCODE_READ 3	 /* Read data bytes */
#define OPCODE_PP 2	 /* Page program */
#define OPCODE_SE 0xD8	 /* Sector erase */
#define OPCODE_RES 0xAB	 /* Read Electronic Signature */
#define OPCODE_RDID 0x9F /* Read JEDEC ID */

#define OPCODE_FAST_READ 0x0B /* Fast Read */
#define OPCODE_DOR 0x3B	      /* Dual Output Read */
#define OPCODE_QOR 0x6B	      /* Quad Output Read */
#define OPCODE_DIOR 0xBB      /* Dual IO High-Performance Read */
#define OPCODE_QIOR 0xEB      /* Quad IO High-Performance Read */
#define OPCODE_READ_ID 0x90   /* Read Manufacturer and Device ID */

#define OPCODE_P4E 0x20 /* 4KB Parameter Sector Erase */
#define OPCODE_P8E 0x40 /* 8KB Parameter Sector Erase */
#define OPCODE_BE 0x60	/* Bulk Erase */
#define OPCODE_BE1 0xC7 /* Bulk Erase */
#define OPCODE_QPP 0x32 /* Quad Page Programing */

#define OPCODE_CLSR 0x30
#define OPCODE_RCR 0x35 /* Read Configuration Register */

#define OPCODE_BRRD 0x16
#define OPCODE_BRWR 0x17

/* Status Register bits. */
#define SR_WIP 1     /* Write in progress */
#define SR_WEL 2     /* Write enable latch */
#define SR_BP0 4     /* Block protect 0 */
#define SR_BP1 8     /* Block protect 1 */
#define SR_BP2 0x10  /* Block protect 2 */
#define SR_EPE 0x20  /* Erase/Program error */
#define SR_SRWD 0x80 /* SR write protect */

struct chip_info
{
	char *name;
	u8 id;
	u32 jedec_id;
	unsigned long sector_size;
	unsigned int n_sectors;
	char addr4b;
};

extern struct chip_info *spi_chip_info;
extern unsigned int bsize;

int snor_wait_ready(int sleep_ms);
int snor_read_sr(u8 *val);
int snor_write_sr(u8 *val);
void snor_write_enable(void);
void snor_write_disable(void);
int snor_unprotect(void);
int snor_4byte_mode(int enable);
int snor_erase_sector(unsigned long offset);
int full_erase_chip(void);

#endif /* SPI_NOR_FLASH_H */

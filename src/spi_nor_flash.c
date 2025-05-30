#include "spi_nor_flash.h"
#include "spi_controller.h"
#include "snorcmd_api.h"
#include "types.h"
#include "timer.h"
#include <stdio.h>
#include <stddef.h>
#include <unistd.h>

// Global variable definitions
struct chip_info *spi_chip_info = NULL;
// unsigned int bsize = 0; // Removed duplicate definition (defined in spi_nand_flash.c)

// Function implementations
int snor_wait_ready(int sleep_ms) {
    int count;
    uint8_t sr = 0;  // Using uint8_t instead of u8
    for (count = 0; count < ((sleep_ms + 1) * 1000); count++) {
        if ((snor_read_sr(&sr)) < 0)
            break;
        else if (!(sr & (SR_WIP | SR_EPE | SR_WEL))) {
            return 0;
        }
        usleep(500); // Use usleep instead of udelay
    }
    printf("%s: read_sr fail: %x\n", __func__, sr);
    return -1;
}

static int snor_read_rg(uint8_t code, uint8_t *val) {  // Using uint8_t
    int retval;
	SPI_CONTROLLER_Chip_Select_Low();
	SPI_CONTROLLER_Write_One_Byte(code);
	retval = SPI_CONTROLLER_Read_NByte(val, 1);
	SPI_CONTROLLER_Chip_Select_High();
	if (retval) {
        printf("%s: ret: %x\n", __func__, retval);
        return -1;
    }
    return 0;
}

static int snor_write_rg(uint8_t code, uint8_t *val) {  // Using uint8_t
    int retval;
	SPI_CONTROLLER_Chip_Select_Low();
	SPI_CONTROLLER_Write_One_Byte(code);
	retval = SPI_CONTROLLER_Write_NByte(val, 1);
	SPI_CONTROLLER_Chip_Select_High();
	if (retval) {
        printf("%s: ret: %x\n", __func__, retval);
        return -1;
    }
    return 0;
}

/* Stray code removed:
    return snor_read_rg(OPCODE_RDSR, val);
}
*/

int snor_write_sr(u8 *val) {
    return snor_write_rg(OPCODE_WRSR, val);
}

void snor_write_enable(void) {
    SPI_CONTROLLER_Chip_Select_Low();
    SPI_CONTROLLER_Write_One_Byte(OPCODE_WREN);
    SPI_CONTROLLER_Chip_Select_High();
}

void snor_write_disable(void) {
    SPI_CONTROLLER_Chip_Select_Low();
    SPI_CONTROLLER_Write_One_Byte(OPCODE_WRDI);
    SPI_CONTROLLER_Chip_Select_High();
}

int snor_unprotect(void) {
    u8 sr = 0;
    if (snor_read_sr(&sr) < 0) {
        printf("%s: read_sr fail: %x\n", __func__, sr);
        return -1;
    }
    if ((sr & (SR_BP0 | SR_BP1 | SR_BP2)) != 0) {
        sr = 0;
        snor_write_sr(&sr);
    }
    return 0;
}

int snor_4byte_mode(int enable) {
    int retval;
    if (snor_wait_ready(1))
        return -1;
    if (spi_chip_info->id == 0x1) { /* Spansion */
        u8 br = enable ? 0x81 : 0;
        snor_write_rg(OPCODE_BRWR, &br);
        u8 br_cfn;
        snor_read_rg(OPCODE_BRRD, &br_cfn);
        if (br_cfn != br) {
            printf("4B mode switch failed %s, 0x%02x, 0x%02x\n", enable ? "enable" : "disable", br_cfn, br);
            return -1;
        }
    } else {
        u8 code = enable ? 0xb7 : 0xe9;
        SPI_CONTROLLER_Chip_Select_Low();
        retval = SPI_CONTROLLER_Write_One_Byte(code);
        SPI_CONTROLLER_Chip_Select_High();
        if (retval) {
            printf("%s: ret: %x\n", __func__, retval);
            return -1;
        }
        if ((!enable) && (spi_chip_info->id == 0xef)) {
            code = 0;
            snor_write_enable();
            snor_write_rg(0xc5, &code);
        }
    }
    return 0;
}

int snor_erase_sector(unsigned long offset) {
    if (snor_wait_ready(950))
        return -1;
    if (spi_chip_info->addr4b) {
        snor_4byte_mode(1);
    }
    snor_write_enable();
    SPI_CONTROLLER_Chip_Select_Low();
    SPI_CONTROLLER_Write_One_Byte(OPCODE_SE);
    if (spi_chip_info->addr4b)
        SPI_CONTROLLER_Write_One_Byte((offset >> 24) & 0xff);
    SPI_CONTROLLER_Write_One_Byte((offset >> 16) & 0xff);
    SPI_CONTROLLER_Write_One_Byte((offset >> 8) & 0xff);
    SPI_CONTROLLER_Write_One_Byte(offset & 0xff);
    SPI_CONTROLLER_Chip_Select_High();
    snor_wait_ready(950);
    if (spi_chip_info->addr4b)
        snor_4byte_mode(0);
    return 0;
}

int full_erase_chip(void) {
    timer_start();
    if (snor_wait_ready(3))
        return -1;
    snor_write_enable();
    snor_unprotect();
    SPI_CONTROLLER_Chip_Select_Low();
    SPI_CONTROLLER_Write_One_Byte(OPCODE_BE1);
    SPI_CONTROLLER_Chip_Select_High();
    snor_wait_ready(950);
    snor_write_disable();
    timer_end();
    return 0;
}

/* Duplicate empty definition removed:
static struct chip_info chips_data [] = {
    // ... (array content)
};
*/
static struct chip_info chips_data [] = {
	/* REVISIT: fill in JEDEC ids, for parts that have them */
	{ "AT26DF161",		0x1f, 0x46000000, 64 * 1024, 32,  0 },
	{ "AT25DF321",		0x1f, 0x47000000, 64 * 1024, 64,  0 },

	{ "A25L10PU",		0x37, 0x20110000, 64 * 1024, 2,   0 },
	{ "A25L20PU",		0x37, 0x20120000, 64 * 1024, 4,   0 },
	{ "A25L040",		0x37, 0x30130000, 64 * 1024, 8,   0 },
	{ "A25LQ080",		0x37, 0x40140000, 64 * 1024, 16,  0 },
	{ "A25L080",		0x37, 0x30140000, 64 * 1024, 16,  0 },
	{ "A25LQ16",		0x37, 0x40150000, 64 * 1024, 32,  0 },
	{ "A25LQ32",		0x37, 0x40160000, 64 * 1024, 64,  0 },
	{ "A25L032",		0x37, 0x30160000, 64 * 1024, 64,  0 },
	{ "A25LQ64",		0x37, 0x40170000, 64 * 1024, 128, 0 },

	{ "ES25P10",		0x4a, 0x20110000, 64 * 1024, 4,   0 },
	{ "ES25P20",		0x4a, 0x20120000, 64 * 1024, 8,   0 },
	{ "ES25P40",		0x4a, 0x20130000, 64 * 1024, 16,  0 },
	{ "ES25P80",		0x4a, 0x20140000, 64 * 1024, 32,  0 },
	{ "ES25P16",		0x4a, 0x20150000, 64 * 1024, 64,  0 },
	{ "ES25P32",		0x4a, 0x20160000, 64 * 1024, 128, 0 },
	{ "ES25M40A",		0x4a, 0x32130000, 64 * 1024, 16,  0 },
	{ "ES25M80A",		0x4a, 0x32140000, 64 * 1024, 32,  0 },
	{ "ES25M16A",		0x4a, 0x32150000, 64 * 1024, 64,  0 },

	{ "DQ25Q64AS",		0x54, 0x40170000, 64 * 1024, 128, 0 },
	{ "DQ25Q128AL",		0x54, 0x60180000, 64 * 1024, 256, 0 },

	{ "F25L016",		0x8c, 0x21150000, 64 * 1024, 32,  0 },
	{ "F25L16QA",		0x8c, 0x41158c41, 64 * 1024, 32,  0 },
	{ "F25L032",		0x8c, 0x21160000, 64 * 1024, 64,  0 },
	{ "F25L32QA",		0x8c, 0x41168c41, 64 * 1024, 64,  0 },
	{ "F25L064",		0x8c, 0x21170000, 64 * 1024, 128, 0 },
	{ "F25L64QA",		0x8c, 0x41170000, 64 * 1024, 128, 0 },

	{ "GD25Q20C",		0xc8, 0x40120000, 64 * 1024, 4,   0 },
	{ "GD25Q40C",		0xc8, 0x40130000, 64 * 1024, 8,   0 },
	{ "GD25Q80C",		0xc8, 0x40140000, 64 * 1024, 16,  0 },
	{ "GD25LQ80C",		0xc8, 0x60140000, 64 * 1024, 16,  0 },
	{ "GD25WD80C",		0xc8, 0x64140000, 64 * 1024, 16,  0 },
	{ "GD25WQ80E",		0xc8, 0x65140000, 64 * 1024, 16,  0 },
	{ "GD25Q16",		0xc8, 0x40150000, 64 * 1024, 32,  0 },
	{ "GD25LQ16C",		0xc8, 0x60150000, 64 * 1024, 32,  0 },
	{ "GD25WQ16E",		0xc8, 0x65150000, 64 * 1024, 32,  0 },
	{ "GD25Q32",		0xc8, 0x40160000, 64 * 1024, 64,  0 },
	{ "GD25LQ32E",		0xc8, 0x60160000, 64 * 1024, 64,  0 },
	{ "GD25WQ32E",		0xc8, 0x65160000, 64 * 1024, 64,  0 },
	{ "GD25Q64CSIG",	0xc8, 0x4017c840, 64 * 1024, 128, 0 },
	{ "GD25LQ64E",		0xc8, 0x60170000, 64 * 1024, 128, 0 },
	{ "GD25Q128CSIG",	0xc8, 0x4018c840, 64 * 1024, 256, 0 },
	{ "GD25LQ128D",		0xc8, 0x60180000, 64 * 1024, 256, 0 },
	{ "GD25F256F",		0xc8, 0x43190000, 64 * 1024, 512, 1 },
	{ "GD25Q256CSIG",	0xc8, 0x4019c840, 64 * 1024, 512, 1 },

	{ "MX25L4005A",		0xc2, 0x2013c220, 64 * 1024, 8,   0 },
	{ "MX25L8005M",		0xc2, 0x2014c220, 64 * 1024, 16,  0 },
	{ "MX25L1605D",		0xc2, 0x2015c220, 64 * 1024, 32,  0 },
	{ "MX25U1635F",		0xc2, 0x2535c220, 64 * 1024, 32,  0 },
	{ "MX25L3205D",		0xc2, 0x2016c220, 64 * 1024, 64,  0 },
	{ "MX25U3235F",		0xc2, 0x2536c220, 64 * 1024, 64,  0 },
	{ "MX25L6405D",		0xc2, 0x2017c220, 64 * 1024, 128, 0 },
	{ "MX25U6435F",		0xc2, 0x2537c220, 64 * 1024, 128, 0 },
	{ "MX25L12805D",	0xc2, 0x2018c220, 64 * 1024, 256, 0 },
	{ "MX25U12835F",	0xc2, 0x2538c220, 64 * 1024, 256, 0 },
	{ "MX25L25635E",	0xc2, 0x2019c220, 64 * 1024, 512, 1 },
	{ "MX25U25643G",	0xc2, 0x2539c220, 64 * 1024, 512, 1 },
	{ "MX25L51245G",	0xc2, 0x201ac220, 64 * 1024, 1024, 1 },
	{ "MX25U51245G",	0xc2, 0x253ac220, 64 * 1024, 1024, 1 },

	{ "YC25Q128",		0xd8, 0x4018d840, 64 * 1024, 256, 0 },

	{ "FL016AIF",		0x01, 0x02140000, 64 * 1024, 32,  0 },
	{ "FL064AIF",		0x01, 0x02160000, 64 * 1024, 128, 0 },
	{ "S25FL016P",		0x01, 0x02144D00, 64 * 1024, 32,  0 },
	{ "S25FL032P",		0x01, 0x02154D00, 64 * 1024, 64,  0 },
	{ "S25FL064P",		0x01, 0x02164D00, 64 * 1024, 128, 0 },
	{ "S25FL128P",		0x01, 0x20180301, 64 * 1024, 256, 0 },
	{ "S25FL129P",		0x01, 0x20184D01, 64 * 1024, 256, 0 },
	{ "S25FL256S",		0x01, 0x02194D01, 64 * 1024, 512, 1 },
	{ "S25FL512S",		0x01, 0x02204D00, 256 * 1024, 256, 1 },
	{ "S25FL116K",		0x01, 0x40150140, 64 * 1024, 32,  0 },
	{ "S25FL132K",		0x01, 0x40160140, 64 * 1024, 64,  0 },
	{ "S25FL164K",		0x01, 0x40170140, 64 * 1024, 128, 0 },

	{ "EN25F16",		0x1c, 0x31151c31, 64 * 1024, 32,  0 },
	{ "EN25Q16",		0x1c, 0x30151c30, 64 * 1024, 32,  0 },
	{ "EN25QH16",		0x1c, 0x70151c70, 64 * 1024, 32,  0 },
	{ "EN25Q32B",		0x1c, 0x30161c30, 64 * 1024, 64,  0 },
	{ "EN25F32",		0x1c, 0x31161c31, 64 * 1024, 64,  0 },
	{ "EN25F64",		0x1c, 0x20171c20, 64 * 1024, 128, 0 },
	{ "EN25Q64",		0x1c, 0x30171c30, 64 * 1024, 128, 0 },
	{ "GM25Q64A",		0x1c, 0x40171c40, 64 * 1024, 128, 0 },
	{ "EN25QA64A",		0x1c, 0x60170000, 64 * 1024, 128, 0 },
	{ "EN25XQ128A",         0x1c, 0x71181c71, 64 * 1024, 256, 0 },
	{ "EN25QH64A",		0x1c, 0x70171c70, 64 * 1024, 128, 0 },
	{ "EN25Q128",		0x1c, 0x30181c30, 64 * 1024, 256, 0 },
	{ "EN25XQ128A",		0x1c, 0x71181c71, 64 * 1024, 256, 0 },
	{ "EN25QA128A",		0x1c, 0x60180000, 64 * 1024, 256, 0 },
	{ "EN25QH128A",		0x1c, 0x70181c70, 64 * 1024, 256, 0 },
	{ "GM25Q128A",		0x1c, 0x40181c40, 64 * 1024, 256, 0 },
	{ "EN25Q256",		0x1c, 0x70191c70, 64 * 1024, 512, 1 },

	{ "W25X05",		0xef, 0x30100000, 64 * 1024, 1,   0 },
	{ "W25X10",		0xef, 0x30110000, 64 * 1024, 2,   0 },
	{ "W25X20",		0xef, 0x30120000, 64 * 1024, 4,   0 },
	{ "W25X40",		0xef, 0x30130000, 64 * 1024, 8,   0 },
	{ "W25X80",		0xef, 0x30140000, 64 * 1024, 16,  0 },
	{ "W25X16",		0xef, 0x30150000, 64 * 1024, 32,  0 },
	{ "W25X32VS",		0xef, 0x30160000, 64 * 1024, 64,  0 },
	{ "W25X64",		0xef, 0x30170000, 64 * 1024, 128, 0 },
	{ "W25Q20CL",		0xef, 0x40120000, 64 * 1024, 4,   0 },
	{ "W25Q20BW",		0xef, 0x50120000, 64 * 1024, 4,   0 },
	{ "W25Q20EW",		0xef, 0x60120000, 64 * 1024, 4,   0 },
	{ "W25Q80",		0xef, 0x50140000, 64 * 1024, 16,  0 },
	{ "W25Q80BL",		0xef, 0x40140000, 64 * 1024, 16,  0 },
	{ "W25Q16JQ",		0xef, 0x40150000, 64 * 1024, 32,  0 },
	{ "W25Q16JM",		0xef, 0x70150000, 64 * 1024, 32,  0 },
	{ "W25Q32BV",		0xef, 0x40160000, 64 * 1024, 64,  0 },
	{ "W25Q32DW",		0xef, 0x60160000, 64 * 1024, 64,  0 },
	{ "W25Q32JWIM",		0xef, 0x80160000, 64 * 1024, 64,  0 },
	{ "W25Q64BV",		0xef, 0x40170000, 64 * 1024, 128, 0 },
	{ "W25Q64DW",		0xef, 0x60170000, 64 * 1024, 128, 0 },
	{ "W25Q64JVIM",		0xef, 0x70170000, 64 * 1024, 128, 0 },
	{ "W25Q64JWIM",		0xef, 0x80170000, 64 * 1024, 128, 0 },
	{ "W25Q128BV",		0xef, 0x40180000, 64 * 1024, 256, 0 },
	{ "W25Q128FW",		0xef, 0x60180000, 64 * 1024, 256, 0 },
	{ "W25Q256FV",		0xef, 0x40190000, 64 * 1024, 512, 1 },
	{ "W25Q256JW",		0xef, 0x60190000, 64 * 1024, 512, 1 },
	{ "W25Q256JWIM",	0xef, 0x80190000, 64 * 1024, 512, 1 },
	{ "W25Q512JV",		0xef, 0x40200000, 64 * 1024, 1024, 1 },
	{ "W25Q512JVIM",	0xef, 0x70200000, 64 * 1024, 1024, 1 },
	{ "W25Q512NW",		0xef, 0x60200000, 64 * 1024, 1024, 1 },
	{ "W25Q512NWIM",	0xef, 0x80200000, 64 * 1024, 1024, 1 },

	{ "M25P05",		0x20, 0x20100000, 64 * 1024, 1,   0 },
	{ "M25P10",		0x20, 0x20110000, 64 * 1024, 2,   0 },
	{ "M25P20",		0x20, 0x20120000, 64 * 1024, 4,   0 },
	{ "M25P40",		0x20, 0x20130000, 64 * 1024, 8,   0 },
	{ "M25P80",		0x20, 0x20140000, 64 * 1024, 16,  0 },
	{ "M25P16",		0x20, 0x20150000, 64 * 1024, 32,  0 },
	{ "M25P32",		0x20, 0x20160000, 64 * 1024, 64,  0 },
	{ "M25P64",		0x20, 0x20170000, 64 * 1024, 128, 0 },
	{ "M25P128",		0x20, 0x20180000, 64 * 1024, 256, 0 },
	{ "N25Q016A",		0x20, 0xbb151000, 64 * 1024, 32,  0 },
	{ "N25Q032A",		0x20, 0xba161000, 64 * 1024, 64,  0 },
	{ "N25Q032A",		0x20, 0xbb161000, 64 * 1024, 64,  0 },
	{ "N25Q064A",		0x20, 0xba171000, 64 * 1024, 128, 0 },
	{ "N25Q064A",		0x20, 0xbb171000, 64 * 1024, 128, 0 },
	{ "N25Q128A",		0x20, 0xba181000, 64 * 1024, 256, 0 },
	{ "N25Q128A",		0x20, 0xbb181000, 64 * 1024, 256, 0 },
	{ "N25Q256A",		0x20, 0xba191000, 64 * 1024, 512, 1 },
	{ "N25Q512A",		0x20, 0xba201000, 64 * 1024, 1024, 1 },
	{ "MT25QL64AB",		0x20, 0xba171000, 64 * 1024, 128, 0 },
	{ "MT25QU64AB",		0x20, 0xbb171000, 64 * 1024, 128, 0 },
	{ "MT25QL128AB",	0x20, 0xba181000, 64 * 1024, 256, 0 },
	{ "MT25QU128AB",	0x20, 0xbb181000, 64 * 1024, 256, 0 },
	{ "MT25QL256AB",	0x20, 0xba191000, 64 * 1024, 512, 1 },
	{ "MT25QU256AB",	0x20, 0xbb191000, 64 * 1024, 512, 1 },
	{ "MT25QL512AB",	0x20, 0xba201044, 64 * 1024, 1024, 1 },
	{ "MT25QU512AB",	0x20, 0xbb201044, 64 * 1024, 1024, 1 },
	{ "XM25QH10B",		0x20, 0x40110000, 64 * 1024, 2,   0 },
	{ "XM25QH20B",		0x20, 0x40120000, 64 * 1024, 4,   0 },
	{ "XM25QU41B",		0x20, 0x50130000, 64 * 1024, 8,   0 },
	{ "XM25QH40B",		0x20, 0x40130000, 64 * 1024, 8,   0 },
	{ "XM25QU80B",		0x20, 0x50140000, 64 * 1024, 16,  0 },
	{ "XM25QH80B",		0x20, 0x40140000, 64 * 1024, 16,  0 },
	{ "XM25QU16B",		0x20, 0x50150000, 64 * 1024, 32,  0 },
	{ "XM25QH16C",		0x20, 0x40150000, 64 * 1024, 32,  0 },
	{ "XM25QW16C",		0x20, 0x42150000, 64 * 1024, 32,  0 },
	{ "XM25QH32B",		0x20, 0x40160000, 64 * 1024, 64,  0 },
	{ "XM25QW32C",		0x20, 0x42160000, 64 * 1024, 64,  0 },
	{ "XM25LU32C",		0x20, 0x50160000, 64 * 1024, 64,  0 },
	{ "XM25QH32A",		0x20, 0x70160000, 64 * 1024, 64,  0 },
	{ "XM25QH64C",		0x20, 0x40170000, 64 * 1024, 128, 0 },
	{ "XM25LU64C",		0x20, 0x41170000, 64 * 1024, 128, 0 },
	{ "XM25QW64C",		0x20, 0x42170000, 64 * 1024, 128, 0 },
	{ "XM25QH64A",		0x20, 0x70170000, 64 * 1024, 128, 0 },
	{ "XM25QH128A",		0x20, 0x70182070, 64 * 1024, 256, 0 },
	{ "XM25QH128C",		0x20, 0x40182070, 64 * 1024, 256, 0 },
	{ "XM25LU128C",		0x20, 0x41180000, 64 * 1024, 256, 0 },
	{ "XM25QW128C",		0x20, 0x42180000, 64 * 1024, 256, 0 },
	{ "XM25QH256C",		0x20, 0x40190000, 64 * 1024, 512, 1 },
	{ "XM25QU256C",		0x20, 0x41190000, 64 * 1024, 512, 1 },
	{ "XM25QW256C",		0x20, 0x42190000, 64 * 1024, 512, 1 },
	{ "XM25QH512C",		0x20, 0x40200000, 64 * 1024, 1024, 1 },
	{ "XM25QU512C",		0x20, 0x41200000, 64 * 1024, 1024, 1 },
	{ "XM25QW512C",		0x20, 0x42200000, 64 * 1024, 1024, 1 },

	{ "MD25D20",		0x51, 0x40120000, 64 * 1024, 4,   0 },
	{ "MD25D40",		0x51, 0x40130000, 64 * 1024, 8,   0 },

	{ "ZB25VQ16",		0x5e, 0x40150000, 64 * 1024, 32,  0 },
	{ "ZB25LQ16",		0x5e, 0x50150000, 64 * 1024, 32,  0 },
	{ "ZB25VQ32",		0x5e, 0x40160000, 64 * 1024, 64,  0 },
	{ "ZB25LQ32",		0x5e, 0x50160000, 64 * 1024, 64,  0 },
	{ "ZB25VQ64",		0x5e, 0x40170000, 64 * 1024, 128, 0 },
	{ "ZB25LQ64",		0x5e, 0x50170000, 64 * 1024, 128, 0 },
	{ "ZB25VQ128",		0x5e, 0x40180000, 64 * 1024, 256, 0 },
	{ "ZB25LQ128",		0x5e, 0x50180000, 64 * 1024, 256, 0 },

	{ "LE25U20AMB",		0x62, 0x06120000, 64 * 1024, 4,   0 },
	{ "LE25U40CMC",		0x62, 0x06130000, 64 * 1024, 8,   0 },

	{ "BY25D05AS",		0x68, 0x40100000, 64 * 1024, 1,   0 },
	{ "BY25D10AS",		0x68, 0x40110000, 64 * 1024, 2,   0 },
	{ "BY25D20AS",		0x68, 0x40120000, 64 * 1024, 4,   0 },
	{ "BY25D40AS",		0x68, 0x40130000, 64 * 1024, 8,   0 },
	{ "BY25Q40BL",		0x68, 0x10130000, 64 * 1024, 8,   0 },
	{ "BY25Q40BL",		0x68, 0x60130000, 64 * 1024, 8,   0 },
	{ "BY25Q80BS",		0x68, 0x40140000, 64 * 1024, 16,  0 },
	{ "BY25Q16BS",		0x68, 0x40150000, 64 * 1024, 32,  0 },
	{ "BY25Q16BL",		0x68, 0x10150000, 64 * 1024, 32,  0 },
	{ "BY25Q32BS",		0x68, 0x40160000, 64 * 1024, 64,  0 },
	{ "BY25Q32AL",		0x68, 0x60160000, 64 * 1024, 64,  0 },
	{ "BY25Q64AS",		0x68, 0x40170000, 64 * 1024, 128, 0 },
	{ "BY25Q64AL",		0x68, 0x60170000, 64 * 1024, 128, 0 },
	{ "BY25Q128AS",		0x68, 0x40180000, 64 * 1024, 256, 0 },
	{ "BY25Q128EL",		0x68, 0x60180000, 64 * 1024, 256, 0 },
	{ "BY25Q256ES",		0x68, 0x40190000, 64 * 1024, 512, 1 },

	{ "XT25F04D",		0x0b, 0x40130000, 64 * 1024, 8,   0 },
	{ "XT25F08B",		0x0b, 0x40140000, 64 * 1024, 16,  0 },
	{ "XT25F08D",		0x0b, 0x60140000, 64 * 1024, 16,  0 },
	{ "XT25F16B",		0x0b, 0x40150000, 64 * 1024, 32,  0 },
	{ "XT25Q16D",		0x0b, 0x60150000, 64 * 1024, 32,  0 },
	{ "XT25F32B",		0x0b, 0x40160000, 64 * 1024, 64,  0 },
	{ "XT25F64B",		0x0b, 0x40170000, 64 * 1024, 128, 0 },
	{ "XT25Q64D",		0x0b, 0x60170000, 64 * 1024, 128, 0 },
	{ "XT25F128B",		0x0b, 0x40180000, 64 * 1024, 256, 0 },
	{ "XT25F128D",		0x0b, 0x60180000, 64 * 1024, 256, 0 },

	{ "PM25LQ016",		0x7f, 0x9d450000, 64 * 1024, 32,  0 },
	{ "PM25LQ032",		0x7f, 0x9d460000, 64 * 1024, 64,  0 },
	{ "PM25LQ064",		0x7f, 0x9d470000, 64 * 1024, 128, 0 },
	{ "PM25LQ128",		0x7f, 0x9d480000, 64 * 1024, 256, 0 },

	{ "IS25LQ010", 		0x9d, 0x40110000, 64 * 1024, 2,   0 },
	{ "IS25LQ020", 		0x9d, 0x40120000, 64 * 1024, 4,   0 },
	{ "IS25WP040D",		0x9d, 0x70130000, 64 * 1024, 8,   0 },
	{ "IS25LP080D",		0x9d, 0x60140000, 64 * 1024, 16,  0 },
	{ "IS25WP080D",		0x9d, 0x70140000, 64 * 1024, 16,  0 },
	{ "IS25LP016D",		0x9d, 0x60150000, 64 * 1024, 32,  0 },
	{ "IS25WP016D",		0x9d, 0x70150000, 64 * 1024, 32,  0 },
	{ "IS25LP032D",		0x9d, 0x60160000, 64 * 1024, 64,  0 },
	{ "IS25WP032D",		0x9d, 0x70160000, 64 * 1024, 64,  0 },
	{ "IS25LP064D",		0x9d, 0x60170000, 64 * 1024, 128, 0 },
	{ "IS25WP064D",		0x9d, 0x70170000, 64 * 1024, 128, 0 },
	{ "IS25LP128F",		0x9d, 0x60180000, 64 * 1024, 256, 0 },
	{ "IS25WP128F",		0x9d, 0x70180000, 64 * 1024, 256, 0 },
	{ "IS25LP256D",		0x9d, 0x60190000, 64 * 1024, 512, 1 },
	{ "IS25WP256D",		0x9d, 0x70190000, 64 * 1024, 512, 1 },
	{ "IS25LP256D",		0x9d, 0x601A0000, 64 * 1024, 1024, 1 },
	{ "IS25WP256D",		0x9d, 0x701A0000, 64 * 1024, 1024, 1 },

	{ "FM25W04",		0xa1, 0x28130000, 64 * 1024, 8,   0 },
	{ "FM25Q04",		0xa1, 0x40130000, 64 * 1024, 8,   0 },
	{ "FM25Q08",		0xa1, 0x40140000, 64 * 1024, 16,  0 },
	{ "FM25W16",		0xa1, 0x28150000, 64 * 1024, 32,  0 },
	{ "FM25Q16",		0xa1, 0x40150000, 64 * 1024, 32,  0 },
	{ "FM25W32",		0xa1, 0x28160000, 64 * 1024, 64,  0 },
	{ "FS25Q32",		0xa1, 0x40160000, 64 * 1024, 64,  0 },
	{ "FM25W64",		0xa1, 0x28170000, 64 * 1024, 128, 0 },
	{ "FS25Q64",		0xa1, 0x40170000, 64 * 1024, 128, 0 },
	{ "FM25W128",		0xa1, 0x28180000, 64 * 1024, 256, 0 },
	{ "FS25Q128",		0xa1, 0x40180000, 64 * 1024, 256, 0 },

	{ "FM25Q04A",		0xf8, 0x32130000, 64 * 1024, 8,	  0 },
	{ "FM25M04A",		0xf8, 0x42130000, 64 * 1024, 8,   0 },
	{ "FM25Q08A",		0xf8, 0x32140000, 64 * 1024, 16,  0 },
	{ "FM25M08A",		0xf8, 0x42140000, 64 * 1024, 16,  0 },
	{ "FM25Q16A",		0xf8, 0x32150000, 64 * 1024, 32,  0 },
	{ "FM25M16A",		0xf8, 0x42150000, 64 * 1024, 32,  0 },
	{ "FM25Q32A",		0xf8, 0x32160000, 64 * 1024, 64,  0 },
	{ "FM25M32B",		0xf8, 0x42160000, 64 * 1024, 64,  0 },
	{ "FM25Q64A",		0xf8, 0x32170000, 64 * 1024, 128, 0 },
	{ "FM25M64A",		0xf8, 0x42170000, 64 * 1024, 128, 0 },
	{ "FM25Q128A",		0xf8, 0x32180000, 64 * 1024, 256, 0 },

	{ "PN25F16",		0xe0, 0x40150000, 64 * 1024, 32,  0 },
	{ "PN25F32",		0xe0, 0x40160000, 64 * 1024, 64,  0 },
	{ "PN25F64",		0xe0, 0x40170000, 64 * 1024, 128, 0 },
	{ "PN25F128",		0xe0, 0x40180000, 64 * 1024, 256, 0 },

	{ "P25D05H",		0x85, 0x60100000, 64 * 1024, 1,   0 },
	{ "P25D10H",		0x85, 0x60110000, 64 * 1024, 2,   0 },
	{ "P25D20H",		0x85, 0x60120000, 64 * 1024, 4,   0 },
	{ "P25D40H",		0x85, 0x60130000, 64 * 1024, 8,   0 },
	{ "P25D80H",		0x85, 0x60140000, 64 * 1024, 16,  0 },
	{ "P25Q16H",		0x85, 0x60150000, 64 * 1024, 32,  0 },
	{ "P25Q32H",		0x85, 0x60160000, 64 * 1024, 64,  0 },
	{ "P25Q64H",		0x85, 0x60170000, 64 * 1024, 128, 0 },
	{ "PY25Q64HA",		0x85, 0x20170000, 64 * 1024, 128, 0 },
	{ "P25Q128H",		0x85, 0x60180000, 64 * 1024, 256, 0 },
	{ "PY25Q64HA",		0x85, 0x20170000, 64 * 1024, 128, 0 },
	{ "PY25Q128HA",		0x85, 0x20180000, 64 * 1024, 256, 0 },

	{ "SK25P32",		0x25, 0x60162560, 64 * 1024, 64,  0 },
	{ "SK25P64",		0x25, 0x60172560, 64 * 1024, 128, 0 },
	{ "SK25P128",		0x25, 0x60182560, 64 * 1024, 256, 0 },

	{ "ZD25Q16A",		0xba, 0x40150000, 64 * 1024, 32,  0 },
	{ "ZD25Q32A",		0xba, 0x40160000, 64 * 1024, 64,  0 },
	{ "ZD25Q64A",		0xba, 0x40170000, 64 * 1024, 128, 0 },
	{ "ZD25Q128A",		0xba, 0x40180000, 64 * 1024, 256, 0 },
	{ "ZD25Q16B",		0xba, 0x32150000, 64 * 1024, 32,  0 },
	{ "ZD25Q32B",		0xba, 0x32160000, 64 * 1024, 64,  0 },
	{ "ZD25Q64B",		0xba, 0x32170000, 64 * 1024, 128, 0 },
	{ "ZD25Q128B",		0xba, 0x32180000, 64 * 1024, 256, 0 },

	{ "PCT25VF010A",	0xbf, 0x49000000, 64 * 1024, 2,   0 },
	{ "PCT25VF020B",	0xbf, 0x258c0000, 64 * 1024, 4,   0 },
	{ "PCT25VF040B",	0xbf, 0x258d0000, 64 * 1024, 8,   0 },
	{ "PCT25VF080B",	0xbf, 0x258e0000, 64 * 1024, 16,  0 },
	{ "PCT25VF016B",	0xbf, 0x25410000, 64 * 1024, 32,  0 },
	{ "PCT25VF032B",	0xbf, 0x254a0000, 64 * 1024, 64,  0 },
	{ "PCT25VF064C",	0xbf, 0x254b0000, 64 * 1024, 128, 0 },
	{ "PCT26VF016",		0xbf, 0x26010000, 64 * 1024, 32,  0 },
	{ "PCT26VF032",		0xbf, 0x26020000, 64 * 1024, 64,  0 },

};

/*
 * read SPI flash device ID
 */
static int snor_read_devid(u8 *rxbuf, int n_rx)
{
	int retval = 0;

	SPI_CONTROLLER_Chip_Select_Low();
	SPI_CONTROLLER_Write_One_Byte(OPCODE_RDID);

	retval = SPI_CONTROLLER_Read_NByte(rxbuf, n_rx);
	SPI_CONTROLLER_Chip_Select_High();
	if (retval) {
		printf("%s: ret: %x\n", __func__, retval);
		return retval;
	}

	return 0;
}

/*
 * read status register
 */
int snor_read_sr(u8 *val)
{
	int retval = 0;

	SPI_CONTROLLER_Chip_Select_Low();
	SPI_CONTROLLER_Write_One_Byte(OPCODE_RDSR);

	retval = SPI_CONTROLLER_Read_NByte(val, 1);
	SPI_CONTROLLER_Chip_Select_High();
	if (retval) {
		printf("%s: ret: %x\n", __func__, retval);
		return retval;
	}

	return 0;
}

struct chip_info *chip_prob(void)
{
	struct chip_info *info = NULL, *match = NULL;
	u8 buf[5];
	u32 jedec, jedec_strip, weight;
	int i;

	snor_read_devid(buf, 5);
	jedec = (u32)((u32)(buf[1] << 24) | ((u32)buf[2] << 16) | ((u32)buf[3] <<8) | (u32)buf[4]);
	jedec_strip = jedec & 0xffff0000;

	printf("spi device id: %x %x %x %x %x (%x)\n", buf[0], buf[1], buf[2], buf[3], buf[4], jedec);

	// FIXME, assign default as AT25D
	weight = 0xffffffff;
	match = &chips_data[0];
	for (i = 0; i < sizeof(chips_data)/sizeof(chips_data[0]); i++) {
		info = &chips_data[i];
		if (info->id == buf[0]) {
			if ((info->jedec_id == jedec) || ((info->jedec_id & 0xffff0000) == jedec_strip)) {
				long int size = (info->sector_size * info->n_sectors);
				if ((size >> 10) >= 1024) {
					printf("Detected SPI NOR Flash: %s, Flash Size: %ld MB\n", info->name, size >> 20);
				} else {
					printf("Detected SPI NOR Flash: %s, Flash Size: %ld KB\n", info->name, size >> 10);
				}
				return info;
			}

			if (weight > (info->jedec_id ^ jedec)) {
				weight = info->jedec_id ^ jedec;
				match = info;
			}
		}
	}
	printf("SPI NOR Flash Not Detected!\n");
	match = NULL; /* Not support JEDEC calculate info */

	return match;
}

long snor_init(void)
{
	spi_chip_info = chip_prob();

	if(spi_chip_info == NULL)
		return -1;

	bsize = spi_chip_info->sector_size;

	return spi_chip_info->sector_size * spi_chip_info->n_sectors;
}

int snor_erase(unsigned long offs, unsigned long len)
{
	unsigned long plen = len;
	// snor_dbg("%s: offs:%x len:%x\n", __func__, offs, len); // Commented out missing function

	/* sanity checks */
	if (len == 0)
		return -1;

	if(!offs && len == (spi_chip_info->sector_size * spi_chip_info->n_sectors))
	{
		printf("Please Wait......\n");
		return full_erase_chip();
	}

	timer_start();

	snor_unprotect();

	/* now erase those sectors */
	while (len > 0) {
		if (snor_erase_sector(offs)) {
			return -1;
		}

		offs += spi_chip_info->sector_size;
		len -= spi_chip_info->sector_size;
		if( timer_progress() )
		{
			printf("\bErase %ld%% [%lu] of [%lu] bytes      ", 100 * (plen - len) / plen, plen - len, plen);
			printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");
			fflush(stdout);
		}
	}
	printf("Erase 100%% [%lu] of [%lu] bytes      \n", plen - len, plen);
	timer_end();

	return 0;
}

int snor_read(unsigned char *buf, unsigned long from, unsigned long len)
{
	u32 read_addr, physical_read_addr, remain_len, data_offset;

	// snor_dbg("%s: from:%x len:%x \n", __func__, from, len); // Commented out missing function

	/* sanity checks */
	if (len == 0)
		return 0;

	timer_start();
	/* Wait till previous write/erase is done. */
	if (snor_wait_ready(1)) {
		/* REVISIT status return?? */
		return -1;
	}

	read_addr = from;
	remain_len = len;

	while(remain_len > 0) {

		physical_read_addr = read_addr;
		data_offset = (physical_read_addr % (spi_chip_info->sector_size));

		if (spi_chip_info->addr4b)
			snor_4byte_mode(1);

		SPI_CONTROLLER_Chip_Select_Low();

		/* Set up the write data buffer. */
		SPI_CONTROLLER_Write_One_Byte(OPCODE_READ);

		if (spi_chip_info->addr4b)
			SPI_CONTROLLER_Write_One_Byte((physical_read_addr >> 24) & 0xff);
		SPI_CONTROLLER_Write_One_Byte((physical_read_addr >> 16) & 0xff);
		SPI_CONTROLLER_Write_One_Byte((physical_read_addr >> 8) & 0xff);
		SPI_CONTROLLER_Write_One_Byte(physical_read_addr & 0xff);

		if( (data_offset + remain_len) < spi_chip_info->sector_size )
		{
			if(SPI_CONTROLLER_Read_NByte(&buf[len - remain_len], remain_len)) {
				SPI_CONTROLLER_Chip_Select_High();
				if (spi_chip_info->addr4b)
					snor_4byte_mode(0);
				len = -1;
				break;
			}
			remain_len = 0;
		} else {
			if(SPI_CONTROLLER_Read_NByte(&buf[len - remain_len], spi_chip_info->sector_size - data_offset)) {
				SPI_CONTROLLER_Chip_Select_High();
				if (spi_chip_info->addr4b)
					snor_4byte_mode(0);
				len = -1;
				break;
			}
			remain_len -= spi_chip_info->sector_size - data_offset;
			read_addr += spi_chip_info->sector_size - data_offset;
			if( timer_progress() ) {
				printf("\bRead %ld%% [%lu] of [%lu] bytes      ", 100 * (len - remain_len) / len, len - remain_len, len);
				printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");
				fflush(stdout);
			}
		}

		SPI_CONTROLLER_Chip_Select_High();

		if (spi_chip_info->addr4b)
			snor_4byte_mode(0);
	}
	printf("Read 100%% [%lu] of [%lu] bytes      \n", len - remain_len, len);
	timer_end();

	return len;
}

int snor_write(unsigned char *buf, unsigned long to, unsigned long len)
{
	u32 page_offset, page_size;
	int rc = 0, retlen = 0;
	unsigned long plen = len;

	// snor_dbg("%s: to:%x len:%x \n", __func__, to, len); // Commented out missing function

	/* sanity checks */
	if (len == 0)
		return 0;

	if (to + len > spi_chip_info->sector_size * spi_chip_info->n_sectors)
		return -1;

	timer_start();
	/* Wait until finished previous write command. */
	if (snor_wait_ready(2)) {
		return -1;
	}


	/* what page do we start with? */
	page_offset = to % FLASH_PAGESIZE;

	if (spi_chip_info->addr4b)
		snor_4byte_mode(1);

	/* write everything in PAGESIZE chunks */
	while (len > 0) {
		page_size = min(len, FLASH_PAGESIZE - page_offset);
		page_offset = 0;
		/* write the next page to flash */

		snor_wait_ready(3);
		snor_write_enable();
		snor_unprotect();

		SPI_CONTROLLER_Chip_Select_Low();
		/* Set up the opcode in the write buffer. */
		SPI_CONTROLLER_Write_One_Byte(OPCODE_PP);

		if (spi_chip_info->addr4b)
			SPI_CONTROLLER_Write_One_Byte((to >> 24) & 0xff);
		SPI_CONTROLLER_Write_One_Byte((to >> 16) & 0xff);
		SPI_CONTROLLER_Write_One_Byte((to >> 8) & 0xff);
		SPI_CONTROLLER_Write_One_Byte(to & 0xff);

		if(!SPI_CONTROLLER_Write_NByte(buf, page_size))
			rc = page_size;
		else
			rc = 1;

		SPI_CONTROLLER_Chip_Select_High();

		// snor_dbg("%s: to:%x page_size:%x ret:%x\n", __func__, to, page_size, rc); // Commented out missing function

		if( timer_progress() ) {
			printf("\bWritten %ld%% [%lu] of [%lu] bytes      ", 100 * (plen - len) / plen, plen - len, plen);
			printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");
			fflush(stdout);
		}

		if (rc > 0) {
			retlen += rc;
			if (rc < page_size) {
				printf("%s: rc:%x page_size:%x\n",
						__func__, rc, page_size);
				snor_write_disable();
				return retlen - rc;
			}
		}

		len -= page_size;
		to += page_size;
		buf += page_size;
	}

	if (spi_chip_info->addr4b)
		snor_4byte_mode(0);

	snor_write_disable();

	printf("Written 100%% [%ld] of [%ld] bytes      \n", plen - len, plen);
	timer_end();

	return retlen;
}

void support_snor_list(void)
{
	int i;

	printf("SPI NOR Flash Support List:\n");
	for ( i = 0; i < (sizeof(chips_data)/sizeof(struct chip_info)); i++)
	{
		printf("%03d. %s\n", i + 1, chips_data[i].name);
	}
}

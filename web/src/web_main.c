/**
 * Web WASM entry point for scriba.
 *
 * Provides exported C functions for the JavaScript web frontend to call:
 *   - scriba_init / scriba_shutdown: USB programmer lifecycle
 *   - scriba_detect_chip: probe flash chip and return type/size/name
 *   - scriba_read_flash / scriba_write_flash / scriba_erase_flash
 *
 * Uses the same flash_cmd interface as the CLI, driven from JS.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "flashcmd_api.h"
#include "spi_controller.h"
#include "spi_nand_flash.h"
#include "spi_nor_flash.h"
#include "snorcmd_api.h"
#include "nandcmd_api.h"

int debug_enabled = 0;
int trace_enabled = 0;

#ifndef SCRIBA_WASM_BUILD
#define SCRIBA_WASM_BUILD "dev"
#endif
static const char build_info[] = "wasmfix-" SCRIBA_WASM_BUILD;

static struct flash_cmd prog;
static long flash_size = -1;
static int chip_detected = 0;
static int flash_type = 0; /* 0=unknown, 1=NOR, 2=NAND */
static char chip_name_buf[128] = {0};

extern unsigned int bsize;

const char *scriba_get_version(void) {
    return build_info;
}

int scriba_init(void) {
    flash_size = -1;
    chip_detected = 0;
    flash_type = 0;
    memset(chip_name_buf, 0, sizeof(chip_name_buf));

    fprintf(stderr, "*** Scriba WASM build: %s ***\n", build_info);
    return spi_controller_init(PROGRAMMER_AUTO);
}

int scriba_init_programmer(int type) {
    flash_size = -1;
    chip_detected = 0;
    flash_type = 0;
    memset(chip_name_buf, 0, sizeof(chip_name_buf));

    return spi_controller_init(type);
}

int scriba_detect_chip(void) {
    flash_size = flash_cmd_init(&prog);
    if (flash_size <= 0)
        return -1;

    if (spi_chip_info != NULL && spi_chip_info->name != NULL) {
        flash_type = 1;
        snprintf(chip_name_buf, sizeof(chip_name_buf), "NOR: %s", spi_chip_info->name);
    } else {
        struct SPI_NAND_FLASH_INFO_T info;
        if (SPI_NAND_Flash_Get_Flash_Info(&info) == 0 && info.ptr_name) {
            flash_type = 2;
            snprintf(chip_name_buf, sizeof(chip_name_buf), "NAND: %s", info.ptr_name);
        } else {
            flash_type = 1;
            snprintf(chip_name_buf, sizeof(chip_name_buf), "Flash (%ld bytes)", flash_size);
        }
    }

    chip_detected = 1;
    return 0;
}

long scriba_get_flash_size(void) {
    return flash_size;
}

const char *scriba_get_chip_name(void) {
    return chip_name_buf;
}

int scriba_get_programmer_type(void) {
    return spi_controller_type();
}

unsigned int scriba_get_block_size(void) {
    return bsize;
}

int scriba_read_flash(unsigned char *buf, unsigned long offset, unsigned long len) {
    if (!chip_detected || !prog.flash_read)
        return -1;
    return prog.flash_read(buf, offset, len);
}

int scriba_write_flash(const unsigned char *buf, unsigned long offset, unsigned long len) {
    if (!chip_detected || !prog.flash_write)
        return -1;
    return prog.flash_write((unsigned char *)buf, offset, len);
}

int scriba_erase_flash(unsigned long offset, unsigned long len) {
    if (!chip_detected || !prog.flash_erase)
        return -1;
    return prog.flash_erase(offset, len);
}

void scriba_shutdown(void) {
    spi_controller_shutdown();
    flash_size = -1;
    chip_detected = 0;
    flash_type = 0;
}

int main(void) {
    return 0;
}

/*
 * scriba.c
 * Shared library implementation of the scriba SPI flash programmer API.
 *
 * Owns global state (debug_enabled, trace_enabled, flash_cmd dispatch table).
 * scriba_init() autodetects a programmer (EZP2019 -> CH341A fallback) and
 * scriba_detect_chip() probes both NOR and NAND flash protocols via
 * flash_cmd_init(). All read/write/erase operations dispatch through the
 * flash_cmd function pointers.
 *
 * Derived from web_main.c — the original WebUSB/WASM entry point from
 * the scriba WebUSB CH341A PR.
 *
 * Copyright (C) 2025-2026 Josh at WLTechBlog <wltechblog@wanderlounge.net>
 * Copyright (C) 2025-2026 Paul Philippov <paul@themactep.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libusb-1.0/libusb.h>

#include "ch341a_spi.h"
#include "ezp2019_spi.h"
#include "flashcmd_api.h"
#include "scriba.h"
#include "spi_controller.h"
#include "spi_nand_flash.h"
#include "spi_nor_flash.h"

/* Global state shared across the library and referenced via extern by
   lower-level modules (ch341a_spi.c, spi_nor_flash.c, etc.). */
int debug_enabled = 0;
int trace_enabled = 0;

#ifndef SCRIBA_BUILD
#define SCRIBA_BUILD "dev"
#endif

/* Dispatch table set by scriba_detect_chip() -> flash_cmd_init(). */
static struct flash_cmd prog;
static long flash_size = -1;
static int chip_detected = 0;

static char chip_name_buf[128] = {0};

extern unsigned int bsize;

void scriba_set_debug(int enable) {
  debug_enabled = enable;
}

int scriba_get_debug(void) {
  return debug_enabled;
}

void scriba_set_trace(int enable) {
  trace_enabled = enable;
  if (enable)
    debug_enabled = 1;
}

const char *scriba_get_version(void) {
  static const char info[] = "scriba-" SCRIBA_BUILD;
  return info;
}

const char *scriba_get_libusb_version(void) {
  return get_libusb_version();
}

struct prog_id {
  uint16_t vid;
  uint16_t pid;
  int type;
  const char *name;
};

static const struct prog_id prog_ids[] = {
    {0x1fc8, 0x310b, SCRIBA_PROGRAMMER_EZP2019, "EZP2019"},
    {0x1fc8, 0x310c, SCRIBA_PROGRAMMER_EZP2019, "EZP2019 Plus"},
    {0x1fc8, 0x310d, SCRIBA_PROGRAMMER_EZP2019, "EZP2023"},
    {0x1a86, 0x5512, SCRIBA_PROGRAMMER_CH341A, "CH341A"},
    {0},
};

int scriba_probe_available(int *types, const char **descriptions,
                           int max_types) {
  libusb_context *ctx = NULL;
  libusb_device **devs;
  struct {
    int type;
    uint8_t bus;
    uint8_t addr;
    uint16_t vid;
    uint16_t pid;
    const char *name;
  } results[SCRIBA_MAX_PROGRAMMERS];
  static char labels[SCRIBA_MAX_PROGRAMMERS][80];
  int count = 0;

  if (libusb_init(&ctx) < 0)
    return 0;

  ssize_t cnt = libusb_get_device_list(ctx, &devs);
  if (cnt < 0) {
    libusb_exit(ctx);
    return 0;
  }

  for (ssize_t i = 0; i < cnt && count < SCRIBA_MAX_PROGRAMMERS; i++) {
    struct libusb_device_descriptor desc;
    if (libusb_get_device_descriptor(devs[i], &desc) < 0)
      continue;

    const struct prog_id *id = prog_ids;
    while (id->vid || id->pid) {
      if (desc.idVendor == id->vid && desc.idProduct == id->pid) {
        results[count].type = id->type;
        results[count].bus = libusb_get_bus_number(devs[i]);
        results[count].addr = libusb_get_device_address(devs[i]);
        results[count].vid = desc.idVendor;
        results[count].pid = desc.idProduct;
        results[count].name = id->name;
        count++;
        break;
      }
      id++;
    }
  }

  libusb_free_device_list(devs, 1);
  libusb_exit(ctx);

  for (int i = 0; i < count; i++) {
    snprintf(labels[i], sizeof(labels[i]), "%04x:%04x Bus %03d Device %03d %s",
             results[i].vid, results[i].pid, results[i].bus, results[i].addr,
             results[i].name);

    if (i < max_types) {
      types[i] = results[i].type;
      if (descriptions)
        descriptions[i] = labels[i];
    }
  }

  return count;
}

/* Auto-detect: try EZP2019 first (it uses the same CH341A chip inside but
   with a different USB VID/PID), then fall back to plain CH341A. */
int scriba_init(void) {
  flash_size = -1;
  chip_detected = 0;
  memset(chip_name_buf, 0, sizeof(chip_name_buf));

  programmer_type = PROGRAMMER_AUTO;

  if (ezp2019_spi_init() == 0) {
    programmer_type = PROGRAMMER_EZP2019;
  } else if (ch341a_spi_init() == 0) {
    programmer_type = PROGRAMMER_CH341A;
  } else {
    return -1;
  }
  SPI_CONTROLLER_Init(programmer_type);
  return 0;
}

int scriba_init_programmer(int type) {
  flash_size = -1;
  chip_detected = 0;
  memset(chip_name_buf, 0, sizeof(chip_name_buf));

  if (type == SCRIBA_PROGRAMMER_CH341A) {
    programmer_type = PROGRAMMER_CH341A;
    int ret = ch341a_spi_init();
    if (ret == 0)
      SPI_CONTROLLER_Init(PROGRAMMER_CH341A);
    return ret;
  } else if (type == SCRIBA_PROGRAMMER_EZP2019) {
    programmer_type = PROGRAMMER_EZP2019;
    int ret = ezp2019_spi_init();
    if (ret == 0)
      SPI_CONTROLLER_Init(PROGRAMMER_EZP2019);
    return ret;
  } else {
    programmer_type = PROGRAMMER_AUTO;
    if (ezp2019_spi_init() == 0) {
      programmer_type = PROGRAMMER_EZP2019;
      SPI_CONTROLLER_Init(PROGRAMMER_EZP2019);
      return 0;
    }
    if (ch341a_spi_init() == 0) {
      programmer_type = PROGRAMMER_CH341A;
      SPI_CONTROLLER_Init(PROGRAMMER_CH341A);
      return 0;
    }
    return -1;
  }
}

int scriba_detect_chip(void) {
  flash_size = flash_cmd_init(&prog);
  if (flash_size <= 0)
    return -1;

  if (spi_chip_info != NULL && spi_chip_info->name != NULL) {
    snprintf(chip_name_buf, sizeof(chip_name_buf), "NOR: %s",
             spi_chip_info->name);
  } else {
    struct SPI_NAND_FLASH_INFO_T info;
    if (SPI_NAND_Flash_Get_Flash_Info(&info) == 0 && info.ptr_name) {
      snprintf(chip_name_buf, sizeof(chip_name_buf), "NAND: %s", info.ptr_name);
    } else {
      snprintf(chip_name_buf, sizeof(chip_name_buf), "Flash (%ld bytes)",
               flash_size);
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
  return (int)programmer_type;
}

unsigned int scriba_get_block_size(void) {
  return bsize;
}

/* All flash operations dispatch through the function pointers set by
   flash_cmd_init(). Returns -1 if no chip was detected or no handler set. */
int scriba_read_flash(unsigned char *buf, unsigned long offset,
                      unsigned long len) {
  if (!chip_detected || !prog.flash_read)
    return -1;
  return prog.flash_read(buf, offset, len);
}

int scriba_write_flash(const unsigned char *buf, unsigned long offset,
                       unsigned long len) {
  if (!chip_detected || !prog.flash_write)
    return -1;
  return prog.flash_write((unsigned char *)buf, offset, len);
}

int scriba_erase_flash(unsigned long offset, unsigned long len) {
  if (!chip_detected || !prog.flash_erase)
    return -1;
  return prog.flash_erase(offset, len);
}

int scriba_reinit(void) {
#ifdef __EMSCRIPTEN__
  return ch341a_spi_reinit();
#else
  return -1;
#endif
}

void scriba_shutdown(void) {
  if (programmer_type == PROGRAMMER_EZP2019)
    ezp2019_spi_shutdown();
  else
    ch341a_spi_shutdown();

  flash_size = -1;
  chip_detected = 0;
}

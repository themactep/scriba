/*
 * spi_nor_flash.c
 * Copyright (C) 2018-2021 McMCC <mcmcc@mail.ru>
 * Copyright (C) 2025-2026 Paul Philippov <paul@themactep.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "spi_nor_flash.h"
#include "snorcmd_api.h"
#include "spi_controller.h"
#include "spi_nor_flash_tables.h"
#include "types.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern int debug_enabled;

// Global variable definitions
struct chip_info *spi_chip_info = NULL;
static bool last_wait_error_was_epe = false;
struct snor_progress_state {
  const char *op;
  unsigned long addr;
};
static struct snor_progress_state snor_progress = {"IDLE", 0};
static void snor_set_progress(const char *op, unsigned long addr) {
  snor_progress.op = op;
  snor_progress.addr = addr;
}

static void snor_clear_progress(void) {
  snor_set_progress("IDLE", 0);
}
// unsigned int bsize = 0; // Removed duplicate definition (defined in
// spi_nand_flash.c)

static bool snor_requires_dual_status(void) {
  if (!spi_chip_info || !spi_chip_info->name)
    return false;
  return strncmp(spi_chip_info->name, "XM", 2) == 0 ||
         strncmp(spi_chip_info->name, "NM", 2) == 0;
}

static bool snor_requires_sr3(void) {
  if (!spi_chip_info || !spi_chip_info->name)
    return false;
  return strncmp(spi_chip_info->name, "XM", 2) == 0 ||
         strncmp(spi_chip_info->name, "NM", 2) == 0 ||
         strncmp(spi_chip_info->name, "ZB25", 4) == 0;
}

static bool snor_is_normem(void) {
  if (!spi_chip_info || !spi_chip_info->name)
    return false;
  return strncmp(spi_chip_info->name, "NM", 2) == 0;
}

static int snor_write_status_block(const u8 *vals, size_t len) {
  int retval;
  if (!len || len > 2)
    return -1;
  u8 buf[3];
  buf[0] = OPCODE_WRSR;
  memcpy(buf + 1, vals, len);
  retval = SPI_CONTROLLER_Write_NByte(buf, 1 + len);
  if (retval) {
    printf("%s: ret: %x\n", __func__, retval);
    return -1;
  }
  return 0;
}

static void snor_clear_status(void) {
  SPI_CONTROLLER_Write_One_Byte(OPCODE_CLSR);
}

static snor_progress_cb_t progress_cb = NULL;

void snor_set_progress_callback(snor_progress_cb_t cb) {
  progress_cb = cb;
}

// Function implementations
int snor_wait_ready(int sleep_ms) {
  uint8_t sr = 0;
  last_wait_error_was_epe = false;
#ifdef __EMSCRIPTEN__
  int count;
  if (sleep_ms < 100) {
    usleep(sleep_ms > 0 ? sleep_ms * 1000 : 1000);
  }
  for (count = 0; count < (sleep_ms + 1) * 5; count++) {
    if ((snor_read_sr(&sr)) < 0)
      break;
    if (sr & SR_EPE) {
      last_wait_error_was_epe = true;
      snor_clear_status();
    }
    if (!(sr & (SR_WIP | SR_EPE))) {
      return 0;
    }
    usleep(100000);
  }
#else
  int delay_us = 500;
  const int max_delay_us = 100000;
  long long max_elapsed_us = (sleep_ms + 1) * 500000LL;
  long long elapsed_us = 0;

  while (elapsed_us < max_elapsed_us) {
    if ((snor_read_sr(&sr)) < 0)
      break;
    if (sr & SR_EPE) {
      last_wait_error_was_epe = true;
      snor_clear_status();
    }
    if (!(sr & (SR_WIP | SR_EPE))) {
      return 0;
    }
    usleep(delay_us);
    elapsed_us += delay_us;
    delay_us *= 2;
    if (delay_us > max_delay_us)
      delay_us = max_delay_us;
  }
#endif
  printf("%s: read_sr fail: %x\n", __func__, sr);
  return -1;
}

static int snor_wait_ready_retry_epe(int sleep_ms) {
  int ret = snor_wait_ready(sleep_ms);
  if (!ret)
    return 0;
  if (debug_enabled)
    fprintf(stderr,
            "[DEBUG] snor_wait_ready_retry_epe: initial wait failed (op=%s "
            "addr=0x%08lx epe=%d)\n",
            snor_progress.op, snor_progress.addr, last_wait_error_was_epe);
  if (!last_wait_error_was_epe)
    return -1;
  if (debug_enabled)
    fprintf(stderr,
            "[DEBUG] snor_wait_ready_retry_epe: retrying after SR_EPE "
            "(sleep=%dms)\n",
            sleep_ms);
  ret = snor_wait_ready(sleep_ms);
  if (ret && debug_enabled)
    fprintf(stderr,
            "[DEBUG] snor_wait_ready_retry_epe: retry failed (op=%s "
            "addr=0x%08lx epe=%d)\n",
            snor_progress.op, snor_progress.addr, last_wait_error_was_epe);
  return ret;
}

static bool snor_wait_error_was_epe(void) {
  return last_wait_error_was_epe;
}

static int snor_read_rg(uint8_t code, uint8_t *val) {
  int retval;
  u8 op = code;
  retval = SPI_CONTROLLER_WriteRead_NByte(&op, 1, val, 1);
  if (retval) {
    printf("%s: ret: %x\n", __func__, retval);
    return -1;
  }
  return 0;
}

static int snor_write_rg(uint8_t code, uint8_t *val) {
  int retval;
  u8 buf[2];
  buf[0] = code;
  buf[1] = *val;
  retval = SPI_CONTROLLER_Write_NByte(buf, 2);
  if (retval) {
    printf("%s: ret: %x\n", __func__, retval);
    return -1;
  }
  return 0;
}

static int snor_read_sr2(u8 *val) {
  return snor_read_rg(OPCODE_RDSR2, val);
}

static int snor_read_sr3(u8 *val) {
  return snor_read_rg(OPCODE_RDSR3, val);
}

static int snor_write_sr2(u8 val) {
  return snor_write_rg(OPCODE_WRSR2, &val);
}

static void snor_volatile_write_enable(void) {
  SPI_CONTROLLER_Write_One_Byte(OPCODE_WREN_VSR);
}

/* NOR-MEM helpers: try non-volatile first, then volatile if needed */
static int snor_write_sr1_nm(u8 sr1_val) {
  snor_write_enable();
  if (snor_write_sr(&sr1_val) == 0 && snor_wait_ready_retry_epe(1) == 0)
    return 0;
  /* fallback to volatile write */
  snor_volatile_write_enable();
  if (snor_write_sr(&sr1_val) == 0 && snor_wait_ready_retry_epe(1) == 0)
    return 0;
  return -1;
}

static int snor_write_sr2_nm(u8 sr2_val) {
  snor_write_enable();
  if (snor_write_sr2(sr2_val) == 0 && snor_wait_ready_retry_epe(1) == 0)
    return 0;
  /* fallback to volatile write */
  snor_volatile_write_enable();
  if (snor_write_sr2(sr2_val) == 0 && snor_wait_ready_retry_epe(1) == 0)
    return 0;
  return -1;
}

static int snor_write_sr3(u8 val) {
  return snor_write_rg(OPCODE_WRSR3, &val);
}

int snor_write_sr(u8 *val) {
  return snor_write_status_block(val, 1);
}
void snor_write_enable(void) {
  SPI_CONTROLLER_Write_One_Byte(OPCODE_WREN);
  if (debug_enabled) {
    u8 sr = 0xff;
    snor_read_sr(&sr);
    fprintf(stderr, "[DEBUG] snor_write_enable: SR after WREN = 0x%02x\n", sr);
  }
}

static int snor_global_block_unlock(void) {
  printf("[INFO] NOR-MEM: Executing Global Block Unlock\n");
  snor_write_enable();
  SPI_CONTROLLER_Write_One_Byte(OPCODE_GBULK);
  return snor_wait_ready_retry_epe(1);
}

static int snor_block_unlock_all(void) {
  if (!spi_chip_info)
    return -1;
  unsigned int n = spi_chip_info->n_sectors;
  unsigned long sec_sz = spi_chip_info->sector_size;
  for (unsigned int i = 0; i < n; i++) {
    unsigned long addr = i * sec_sz;
    u8 buf[4];
    buf[0] = OPCODE_SBULK;
    buf[1] = (addr >> 16) & 0xFF;
    buf[2] = (addr >> 8) & 0xFF;
    buf[3] = addr & 0xFF;
    snor_write_enable();
    SPI_CONTROLLER_Write_NByte(buf, 4);
    if (snor_wait_ready_retry_epe(1))
      return -1;
  }
  return 0;
}

static void snor_reset_chip(void) {
  SPI_CONTROLLER_Write_One_Byte(OPCODE_RSTEN);
  SPI_CONTROLLER_Write_One_Byte(OPCODE_RST);
  usleep(1000); /* allow reset to complete */
}

void snor_write_disable(void) {
  SPI_CONTROLLER_Write_One_Byte(OPCODE_WRDI);
}

int snor_unprotect(void) {
  u8 sr1 = 0;
  u8 sr2 = 0;
  u8 sr3 = 0;
  bool needs_sr2 = snor_requires_dual_status();
  bool needs_sr3 = snor_requires_sr3();
  u8 bp_mask = SR_BP0 | SR_BP1 | SR_BP2;
  if (snor_is_normem()) {
    /* NOR-MEM chips use 5 bits (BP4-BP0) in SR1 plus CMP bit */
    bp_mask = SR_BP0 | SR_BP1 | SR_BP2 | SR_BP3 | SR_BP4 | SR_CMP;
  }
  if (snor_read_sr(&sr1) < 0) {
    printf("%s: read_sr fail: %x\n", __func__, sr1);
    return -1;
  }
  if (needs_sr2) {
    if (snor_read_sr2(&sr2) < 0)
      needs_sr2 = false;
  }
  if (needs_sr3) {
    if (snor_read_sr3(&sr3) < 0)
      needs_sr3 = false;
  }
  if (debug_enabled) {
    fprintf(stderr,
            "[DEBUG] snor_unprotect: SR1=%02x SR2=%02x SR3=%02x (need_sr2=%d "
            "need_sr3=%d)\n",
            sr1, needs_sr2 ? sr2 : 0, needs_sr3 ? sr3 : 0, needs_sr2,
            needs_sr3);
  }

  /* For NOR-MEM chips, try Global Block Unlock first */
  if (snor_is_normem()) {
    snor_clear_status();
    snor_write_disable();
    snor_reset_chip();
    if (snor_block_unlock_all() < 0)
      printf("[WARN] Block unlock (SBULK) loop failed\n");
    if (snor_global_block_unlock() < 0) {
      printf("[WARN] Global Block Unlock failed\n");
    }
    /* Re-read status registers after unlock */
    if (snor_read_sr(&sr1) < 0) {
      printf("%s: read_sr fail after GBULK: %x\n", __func__, sr1);
      return -1;
    }
    if (needs_sr2 && snor_read_sr2(&sr2) < 0)
      needs_sr2 = false;
    if (needs_sr3 && snor_read_sr3(&sr3) < 0)
      needs_sr3 = false;
    printf("[INFO] After GBULK: SR1=%02x SR2=%02x SR3=%02x\n", sr1,
           needs_sr2 ? sr2 : 0, needs_sr3 ? sr3 : 0);
  }

  bool clear_sr1 = sr1 & bp_mask;
  bool clear_sr2_srp = false;
  if (snor_is_normem() && needs_sr2) {
    /* NOR-MEM chips: also need to clear SRP1 (bit 1) and SRP0 (bit 0) in SR2 */
    clear_sr2_srp = sr2 & 0x03;
  }
  bool clear_sr3_bp = needs_sr3 && (sr3 & SR3_BP3);
  bool clear_sr3_tb = needs_sr3 && (sr3 & SR3_TB);
  bool clear_sr3_wps = needs_sr3 && (sr3 & SR3_WPS);
  bool clear_sr3 = clear_sr3_bp || clear_sr3_tb || clear_sr3_wps;
  if (!clear_sr1 && !clear_sr3 && !clear_sr2_srp)
    return 0;

  if (clear_sr3) {
    u8 cleared_sr3 = sr3 & ~(SR3_BP3 | SR3_TB | SR3_WPS);
    snor_write_enable();
    if (snor_write_sr3(cleared_sr3) < 0 || snor_wait_ready_retry_epe(1)) {
      /* fallback: try volatile write of SR3 */
      snor_volatile_write_enable();
      if (snor_write_sr3(cleared_sr3) < 0 || snor_wait_ready_retry_epe(1)) {
        if (!snor_wait_error_was_epe())
          return -1;
        if (debug_enabled)
          fprintf(stderr, "[DEBUG] snor_unprotect: continuing after SR_EPE "
                          "while clearing SR3 (volatile)\n");
      }
    }
  }

  if (clear_sr1 || clear_sr2_srp) {
    if (snor_is_normem()) {
      /* NOR-MEM chips: clear SR2 (SRP) before SR1 */
      if (clear_sr2_srp) {
        u8 cleared_sr2 = sr2 & ~0x03;
        printf("[INFO] NOR-MEM: Clearing SR2 from 0x%02x to 0x%02x\n", sr2,
               cleared_sr2);
        if (snor_write_sr2_nm(cleared_sr2) < 0)
          return -1;
      }
      if (clear_sr1) {
        u8 cleared_sr1 = sr1 & ~bp_mask;
        printf("[INFO] NOR-MEM: Clearing SR1 from 0x%02x to 0x%02x "
               "(mask=0x%02x)\n",
               sr1, cleared_sr1, bp_mask);
        if (snor_write_sr1_nm(cleared_sr1) < 0)
          return -1;
      }
    } else {
      /* Standard chips write SR1 and SR2 together */
      u8 buf[2];
      size_t len = 0;
      buf[len++] = sr1 & ~bp_mask;
      if (needs_sr2)
        buf[len++] = sr2;
      snor_write_enable();
      if (snor_write_status_block(buf, len) < 0)
        return -1;
      if (snor_wait_ready_retry_epe(1)) {
        if (!snor_wait_error_was_epe())
          return -1;
        if (debug_enabled)
          fprintf(stderr, "[DEBUG] snor_unprotect: continuing after SR_EPE "
                          "while clearing SR1/SR2\n");
      }
    }
  }

  if (snor_read_sr(&sr1) < 0) {
    printf("%s: read_sr fail: %x\n", __func__, sr1);
    return -1;
  }
  if (needs_sr2 && snor_read_sr2(&sr2) < 0)
    needs_sr2 = false;
  if (needs_sr3 && snor_read_sr3(&sr3) < 0)
    needs_sr3 = false;
  printf("[INFO] snor_unprotect after clear: SR1=%02x SR2=%02x SR3=%02x\n", sr1,
         needs_sr2 ? sr2 : 0, needs_sr3 ? sr3 : 0);
  if (debug_enabled) {
    fprintf(stderr, "[DEBUG] snor_unprotect: SR1'=%02x SR2'=%02x SR3'=%02x\n",
            sr1, needs_sr2 ? sr2 : 0, needs_sr3 ? sr3 : 0);
  }
  if (sr1 & bp_mask) {
    printf("%s: unable to clear block protection (SR=%02x)\n", __func__, sr1);
    return -1;
  }
  if (needs_sr3 && (sr3 & SR3_BP3)) {
    printf("%s: unable to clear extended block protection (SR3=%02x)\n",
           __func__, sr3);
    return -1;
  }
  if (needs_sr3 && (sr3 & SR3_TB)) {
    printf("%s: unable to clear Top/Bottom protect (SR3=%02x)\n", __func__,
           sr3);
    return -1;
  }
  if (needs_sr3 && (sr3 & SR3_WPS)) {
    printf("%s: unable to clear write protect selection (SR3=%02x)\n", __func__,
           sr3);
    return -1;
  }
  return 0;
}

int snor_4byte_mode(int enable) {
  int retval;
  if (snor_wait_ready_retry_epe(1))
    return -1;
  if (spi_chip_info->id == 0x1) { /* Spansion */
    u8 br = enable ? 0x81 : 0;
    snor_write_rg(OPCODE_BRWR, &br);
    u8 br_cfn;
    snor_read_rg(OPCODE_BRRD, &br_cfn);
    if (br_cfn != br) {
      printf("4B mode switch failed %s, 0x%02x, 0x%02x\n",
             enable ? "enable" : "disable", br_cfn, br);
      return -1;
    }
  } else {
    u8 code = enable ? 0xb7 : 0xe9;
    retval = SPI_CONTROLLER_Write_One_Byte(code);
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
  snor_set_progress("SE", offset);
  if (snor_wait_ready_retry_epe(950)) {
    snor_clear_progress();
    return -1;
  }
  if (spi_chip_info->addr4b)
    snor_4byte_mode(1);
  snor_write_enable();
  unsigned char addr_buf[5];
  int addr_len = 0;
  addr_buf[addr_len++] = OPCODE_SE;
  if (spi_chip_info->addr4b)
    addr_buf[addr_len++] = (offset >> 24) & 0xff;
  addr_buf[addr_len++] = (offset >> 16) & 0xff;
  addr_buf[addr_len++] = (offset >> 8) & 0xff;
  addr_buf[addr_len++] = offset & 0xff;
  if (SPI_CONTROLLER_Write_NByte(addr_buf, addr_len)) {
    if (spi_chip_info->addr4b)
      snor_4byte_mode(0);
    snor_clear_progress();
    return -1;
  }
  if (snor_wait_ready(950)) {
    if (spi_chip_info->addr4b)
      snor_4byte_mode(0);
    snor_clear_progress();
    return -1;
  }
  if (spi_chip_info->addr4b)
    snor_4byte_mode(0);
  snor_clear_progress();
  return 0;
}

int full_erase_chip(void) {
  if (snor_wait_ready_retry_epe(3)) {
    if (!snor_wait_error_was_epe())
      return -1;
    if (debug_enabled)
      fprintf(stderr, "[DEBUG] full_erase_chip: continuing after SR_EPE to "
                      "clear protection\n");
  }
  if (snor_unprotect()) {
    return -1;
  }
  snor_set_progress("BE", 0);
  snor_write_enable();
  if (SPI_CONTROLLER_Write_One_Byte(OPCODE_BE1)) {
    snor_write_disable();
    snor_clear_progress();
    return -1;
  }

  /* wait for bulk erase completion with spinner */
  {
    const char spinner[] = "|/-\\";
    int sp = 0;
    uint8_t sr = 0;
    int delay_us = 500;
    const int max_delay_us = 100000;
    long long max_us = 951 * 500000LL;
    long long elapsed_us = 0;

    while (elapsed_us < max_us) {
      if ((snor_read_sr(&sr)) < 0)
        break;
      if (sr & SR_EPE) {
        last_wait_error_was_epe = true;
        snor_clear_status();
      }
      if (!(sr & (SR_WIP | SR_EPE))) {
        return 0;
      }
      if (elapsed_us >= 1000000)
        printf("\rPlease Wait...... %c", spinner[sp++ % 4]);
      fflush(stdout);
      usleep(delay_us);
      elapsed_us += delay_us;
      delay_us *= 2;
      if (delay_us > max_delay_us)
        delay_us = max_delay_us;
    }
    printf("%s: read_sr fail: %x\n", __func__, sr);
    snor_write_disable();
    snor_clear_progress();
    return -1;
  }
}

/*
 * read SPI flash device ID
 */
static int snor_read_devid(u8 *rxbuf, int n_rx) {
  int retval;
  u8 op = OPCODE_RDID;
  retval = SPI_CONTROLLER_WriteRead_NByte(&op, 1, rxbuf, n_rx);
  if (retval) {
    printf("%s: ret: %x\n", __func__, retval);
    return retval;
  }
  return 0;
}

/*
 * read status register
 */
int snor_read_sr(u8 *val) {
  int retval;
  u8 op = OPCODE_RDSR;
  retval = SPI_CONTROLLER_WriteRead_NByte(&op, 1, val, 1);
  if (retval) {
    printf("%s: ret: %x\n", __func__, retval);
    return retval;
  }
  return 0;
}

struct chip_info *chip_prob(void) {
  struct chip_info *info = NULL, *match = NULL;
  u8 buf[5];
  u32 jedec, jedec_strip, weight;
  int i;

  snor_read_devid(buf, 5);
  jedec = (u32)((u32)(buf[1] << 24) | ((u32)buf[2] << 16) | ((u32)buf[3] << 8) |
                (u32)buf[4]);
  jedec_strip = jedec & 0xffff0000;

  if (debug_enabled)
    printf("spi device id: %x %x %x %x %x (%x)\n", buf[0], buf[1], buf[2],
           buf[3], buf[4], jedec);

  /* Primary lookup: binary search in sorted table (O(log n)) */
  info = chip_prob_binary_search(buf[0], jedec, jedec_strip);
  if (info)
    return info;

  /* Fallback: linear search for closest weight match */
  weight = 0xffffffff;
  match = NULL;
  for (i = 0; i < chips_data_count; i++) {
    info = &chips_data[i];
    if (info->id == buf[0]) {
      if (weight > (info->jedec_id ^ jedec)) {
        weight = info->jedec_id ^ jedec;
        match = info;
      }
    }
  }

  return match;
}

long snor_init(void) {
  spi_chip_info = chip_prob();

  if (spi_chip_info == NULL)
    return -1;

  bsize = spi_chip_info->sector_size;

  unsigned long flash_size =
      spi_chip_info->sector_size * spi_chip_info->n_sectors;
  SPI_CONTROLLER_Set_Flash_Params((uint32_t)flash_size, 256, 0);

  return flash_size;
}

int snor_erase(unsigned long offs, unsigned long len) {
  unsigned long plen = len;
  unsigned long full_span =
      spi_chip_info->sector_size * spi_chip_info->n_sectors;
  // snor_dbg("%s: offs:%x len:%x\n", __func__, offs, len); // Commented out
  // missing function

  /* sanity checks */
  if (len == 0)
    return -1;

  if (!offs && len == full_span) {
    printf("Please Wait......");
    fflush(stdout);
    if (full_erase_chip() == 0) {
      if (progress_cb)
        progress_cb("Erase", plen, plen);
      return 0;
    }
    printf("[WARN] Bulk erase failed, falling back to sector erase.\n");
  }

  snor_unprotect();

  /* now erase those sectors */
  while (len > 0) {
    if (snor_erase_sector(offs)) {
      return -1;
    }

    offs += spi_chip_info->sector_size;
    len -= spi_chip_info->sector_size;
    if (progress_cb)
      progress_cb("Erase", plen - len, plen);
  }

  return 0;
}

#define SNOR_READ_CHUNK 65536

int snor_read(unsigned char *buf, unsigned long from, unsigned long len) {
  if (len == 0)
    return 0;

  /* Wait till previous write/erase is done. */
  if (snor_wait_ready_retry_epe(1))
    return -1;

  unsigned long read_addr = from;
  unsigned long remain_len = len;

  while (remain_len > 0) {
    unsigned long chunk = remain_len;
#ifdef __EMSCRIPTEN__
    if (chunk > 4096)
      chunk = 4096;
#else
    if (chunk > SNOR_READ_CHUNK)
      chunk = SNOR_READ_CHUNK;
#endif

    if (spi_chip_info->addr4b)
      snor_4byte_mode(1);

    unsigned char cmd[5];
    int cmd_len = 0;
    cmd[cmd_len++] = OPCODE_READ;
    if (spi_chip_info->addr4b)
      cmd[cmd_len++] = (read_addr >> 24) & 0xff;
    cmd[cmd_len++] = (read_addr >> 16) & 0xff;
    cmd[cmd_len++] = (read_addr >> 8) & 0xff;
    cmd[cmd_len++] = read_addr & 0xff;

    if (SPI_CONTROLLER_WriteRead_NByte(cmd, cmd_len,
                                       &buf[len - remain_len], chunk)) {
      if (spi_chip_info->addr4b)
        snor_4byte_mode(0);
      return -1;
    }

    if (spi_chip_info->addr4b)
      snor_4byte_mode(0);

    remain_len -= chunk;
    read_addr += chunk;

    if (progress_cb)
      progress_cb("Read", len - remain_len, len);
  }

  return len;
}

int snor_write(unsigned char *buf, unsigned long to, unsigned long len) {
  u32 page_offset, page_size;
  int rc = 0, retlen = 0;
  int err = 0;
  unsigned long plen = len;

  // snor_dbg("%s: to:%x len:%x \n", __func__, to, len); // Commented out
  // missing function

  /* sanity checks */
  if (len == 0)
    return 0;

  if (to + len > spi_chip_info->sector_size * spi_chip_info->n_sectors)
    return -1;

  /* Wait until finished previous write command. */
  if (snor_wait_ready_retry_epe(2)) {
    if (!snor_wait_error_was_epe())
      return -1;
    if (debug_enabled)
      fprintf(
          stderr,
          "[DEBUG] snor_write: continuing after SR_EPE before programming\n");
  }

  /* unprotect once before the write loop */
  if (snor_unprotect()) {
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
    snor_set_progress("PP", to);
    snor_write_enable();

    /* combine opcode + address + data into one USB transfer */
    unsigned char cmd_buf[5 + FLASH_PAGESIZE];
    int cmd_len = 0;
    cmd_buf[cmd_len++] = OPCODE_PP;
    if (spi_chip_info->addr4b)
      cmd_buf[cmd_len++] = (to >> 24) & 0xff;
    cmd_buf[cmd_len++] = (to >> 16) & 0xff;
    cmd_buf[cmd_len++] = (to >> 8) & 0xff;
    cmd_buf[cmd_len++] = to & 0xff;
    memcpy(cmd_buf + cmd_len, buf, page_size);
    cmd_len += page_size;

    rc = SPI_CONTROLLER_Write_NByte(cmd_buf, cmd_len) ? 1 : page_size;

    // snor_dbg("%s: to:%x page_size:%x ret:%x\n", __func__, to, page_size, rc);
    // // Commented out missing function

    if (rc > 0) {
      retlen += rc;
      if (rc < (int)page_size) {
        printf("%s: rc:%x page_size:%x\n", __func__, rc, page_size);
        snor_write_disable();
        return retlen - rc;
      }
    }

    if (snor_wait_ready(3)) {
      err = -1;
      break;
    }

    len -= page_size;
    to += page_size;
    buf += page_size;

    if (progress_cb)
      progress_cb("Written", plen - len, plen);
  }

  if (!err) {
    if (snor_wait_ready(3))
      err = -1;
  }

  if (spi_chip_info->addr4b)
    snor_4byte_mode(0);

  snor_write_disable();
  snor_clear_progress();

  if (err) {
    return err;
  }

  return retlen;
}

void support_snor_list(void) {
  int i;

  printf("SPI NOR Flash Support List:\n");
  for (i = 0; i < chips_data_count; i++) {
    printf("%03d. %s\n", i + 1, chips_data[i].name);
  }
}

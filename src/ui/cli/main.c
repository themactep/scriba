/*
 * src/ui/cli/main.c
 * Scriba CLI frontend — command-line flash programmer interface.
 *
 * Thin dispatch layer: parses arguments, initialises programmer/chip,
 * delegates operations to src/core/operations.c.
 *
 * Copyright (C) 2025-2026 Paul Philippov <paul@themactep.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <getopt.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef GIT_COMMIT_DATE
#define GIT_COMMIT_DATE "unknown"
#endif
#ifndef GIT_COMMIT_HASH
#define GIT_COMMIT_HASH "unknown"
#endif

#include "ch341a_i2c.h"
#include "ch341a_spi.h"
#include "ezp2019_spi.h"
#include "flashcmd_api.h"
#include "operations.h"
#include "scriba.h"
#include "spi_controller.h"
#include "spi_nand_flash.h"
#include "spi_nor_flash.h"
#include "timer.h"

extern unsigned int bsize;

#include "bitbang_microwire.h"
#include "spi_eeprom.h"
extern struct EEPROM eeprom_info;
extern struct spi_eeprom seeprom_info;
extern char eepromname[12];
extern int eepromsize;
extern int seepromsize;
extern int mw_eepromsize;
extern int spage_size;
extern int org;

static void title(void) {
  printf("Scriba (Thingino Programming Tool) v.%s-%s\n", GIT_COMMIT_DATE,
         GIT_COMMIT_HASH);
#ifdef CONFIG_STATIC
  printf("Static");
#else
  printf("Dynamic");
#endif
  printf(" build using libusb %s\n", scriba_get_libusb_version());
}

static void usage(const char *program_name) {
  char use[1024];
  snprintf(
      use, sizeof(use),
      "\nUsage: %s [options]\n"
      "Automation:\n"
      "  -R <file>    Read chip (read twice and compare)\n"
      "  -W <file>    Write chip (erase + write + verify)\n"
      "\n"
      "Single operations:\n"
      "  -i           Read chip ID\n"
      "  -e           Erase chip\n"
      "  -r <file>    Read chip to file\n"
      "  -w <file>    Write file to chip\n"
      "  -v           Verify after write\n"
      "\n"
      "Granularity:\n"
      "  -a <address> Set address\n"
      "  -l <bytes>   Set length\n"
      "\n"
      "SPI NAND:\n"
      "  -d           Disable internal ECC\n"
      "  -o <bytes>   Set OOB size\n"
      "  -I           Ignore ECC errors\n"
      "  -k           Skip BAD pages\n"
      "\n"
      "EEPROM:\n"
      "  -E <chip>    Select EEPROM type\n"
      "  -8           Set 8-bit organization\n"
      "  -f <bits>    Set address size\n"
      "  -s <bytes>   Set page size\n"
      "\n"
      "General:\n"
      "  -h           Display help\n"
      "  -L           List supported chips\n"
      "  -P <prog>    Programmer type: ch341a, ezp2019, auto (default: auto)\n"
      "  --debug      Enable debug messages for USB communication\n"
      "  --trace      Dump SPI commands and data (implies --debug)\n",
      program_name);
  printf(use);
  exit(0);
}

static void cli_progress_cb(const char *op, unsigned long current,
                            unsigned long total) {
  static time_t start_time = 0;
  static time_t last_print = 0;

  if (current >= total) {
    unsigned long elapsed = start_time ? (time(NULL) - start_time) : 0;
    if (scriba_get_debug())
      printf("\r%s 100%% [%8lu] of [%lu] bytes", op, current, total);
    else
      printf("\r%s 100%%", op);
    if (elapsed > 0) {
      unsigned long bps = current / elapsed;
      if (bps >= 1048576)
        printf(" - %lu MB/s", bps / 1048576);
      else if (bps >= 1024)
        printf(" - %lu KB/s", bps / 1024);
      else
        printf(" - %lu B/s", bps);
    }
    printf("                         \n");
    last_print = 0;
    start_time = 0;
    return;
  }

  time_t now = time(NULL);
  if (now != last_print) {
    if (!last_print) {
      timer_start();
      start_time = now;
    }
    unsigned long elapsed = now - start_time;
    if (elapsed > 0) {
      unsigned long bps = current / elapsed;
      const char *unit;
      unsigned long rate;
      if (bps >= 1048576) {
        unit = "MB/s";
        rate = bps / 1048576;
      } else if (bps >= 1024) {
        unit = "KB/s";
        rate = bps / 1024;
      } else {
        unit = "B/s";
        rate = bps;
      }
      if (scriba_get_debug())
        printf("\r%s %d%% [%8lu] of [%lu] bytes - %lu %s   ", op,
               (int)(100 * current / total), current, total, rate, unit);
      else
        printf("\r%s %d%% at %lu %s   ", op, (int)(100 * current / total), rate,
               unit);
    } else {
      if (scriba_get_debug())
        printf("\r%s %d%% [%8lu] of [%lu] bytes   ", op,
               (int)(100 * current / total), current, total);
      else
        printf("\r%s %d%%   ", op, (int)(100 * current / total));
    }
    fflush(stdout);
    last_print = now;
  }
}

static void handle_sigint(int sig) {
  signal(sig, SIG_DFL);
  fprintf(stderr, "\nInterrupted, shutting down...\n");
  scriba_shutdown();
  _exit(1);
}

int main(int argc, char *argv[]) {
  signal(SIGINT, handle_sigint);
  int c, vr = 0;
  char op = 0;
  const char *op_arg = NULL;
  long long len = 0, addr = 0;
  long flen = 0;
  int exit_code = 1;

  static struct option long_options[] = {{"debug", no_argument, NULL, 0},
                                         {"trace", no_argument, NULL, 0},
                                         {0, 0, 0, 0}};
  int option_index = 0;

  while ((c = getopt_long(argc, argv, "diIhveLkl:a:w:r:W:R:o:s:E:f:8P:",
                          long_options, &option_index)) != -1) {
    if (c == 0) {
      const char *lname = long_options[option_index].name;
      if (strcmp(lname, "debug") == 0) {
        scriba_set_debug(1);
        printf("Debug mode enabled\n");
        continue;
      }
      if (strcmp(lname, "trace") == 0) {
        scriba_set_trace(1);
        printf("Trace mode enabled (debug forced on)\n");
        continue;
      }
    }
    switch (c) {
    case 'E':
      if ((eepromsize = parseEEPsize(optarg, &eeprom_info)) > 0) {
        memset(eepromname, 0, sizeof(eepromname));
        strncpy(eepromname, optarg, 10);
        if (len > eepromsize) {
          fprintf(stderr, "Error set size %lld, max size %d for EEPROM %s!\n",
                  len, eepromsize, eepromname);
          exit(1);
        }
      } else if ((mw_eepromsize = deviceSize_3wire(optarg)) > 0) {
        memset(eepromname, 0, sizeof(eepromname));
        strncpy(eepromname, optarg, 10);
        org = 1;
        if (len > mw_eepromsize) {
          fprintf(stderr, "Error set size %lld, max size %d for EEPROM %s!\n",
                  len, mw_eepromsize, eepromname);
          exit(1);
        }
      } else if ((seepromsize = parseSEEPsize(optarg, &seeprom_info)) > 0) {
        memset(eepromname, 0, sizeof(eepromname));
        strncpy(eepromname, optarg, 10);
        if (len > seepromsize) {
          fprintf(stderr, "Error set size %lld, max size %d for EEPROM %s!\n",
                  len, seepromsize, eepromname);
          exit(1);
        }
      } else {
        fprintf(stderr, "Unknown EEPROM chip %s!\n", optarg);
        exit(1);
      }
      break;
    case '8':
      if (mw_eepromsize <= 0) {
        fprintf(stderr, "-8 option only for Microwire EEPROM chips!\n");
        exit(1);
      }
      org = 0;
      break;
    case 'f':
      if (mw_eepromsize <= 0) {
        fprintf(stderr, "-f option only for Microwire EEPROM chips!\n");
        exit(1);
      }
      fix_addr_len =
          strtoll(optarg, NULL, *optarg && *(optarg + 1) == 'x' ? 16 : 10);
      if (fix_addr_len > 32) {
        fprintf(stderr, "Address len is very big!\n");
        exit(1);
      }
      break;
    case 's':
      spage_size =
          strtoll(optarg, NULL, *optarg && *(optarg + 1) == 'x' ? 16 : 10);
      break;
    case 'I':
      ECC_ignore = 1;
      break;
    case 'k':
      Skip_BAD_page = 1;
      break;
    case 'd':
      ECC_fcheck = 0;
      _ondie_ecc_flag = 0;
      break;
    case 'l':
      len = strtoll(optarg, NULL, *optarg && *(optarg + 1) == 'x' ? 16 : 10);
      break;
    case 'o':
      OOB_size =
          strtoll(optarg, NULL, *optarg && *(optarg + 1) == 'x' ? 16 : 10);
      break;
    case 'a':
      addr = strtoll(optarg, NULL, *optarg && *(optarg + 1) == 'x' ? 16 : 10);
      break;
    case 'v':
      vr = 1;
      break;
    case 'i':
    case 'e':
      if (!op)
        op = c;
      else
        op = 'x';
      break;
    case 'r':
    case 'w':
    case 'W':
    case 'R':
      if (!op) {
        op = c;
        op_arg = optarg;
      } else
        op = 'x';
      break;
    case 'P':
      if (strcmp(optarg, "ezp2019") == 0 || strcmp(optarg, "ezp") == 0)
        programmer_type = PROGRAMMER_EZP2019;
      else if (strcmp(optarg, "ch341a") == 0 || strcmp(optarg, "ch341") == 0)
        programmer_type = PROGRAMMER_CH341A;
      else if (strcmp(optarg, "auto") == 0)
        programmer_type = PROGRAMMER_AUTO;
      else {
        fprintf(stderr,
                "Unknown programmer type: %s (use ch341a, ezp2019, or auto)\n",
                optarg);
        exit(1);
      }
      SPI_CONTROLLER_Init(programmer_type);
      break;
    case 'L':
      support_flash_list();
      exit(0);
    case 'h':
    default:
      usage(argv[0]);
    }
  }

  if (op == 0)
    usage(argv[0]);

  if (op == 'x' || (ECC_ignore && !ECC_fcheck) ||
      (ECC_ignore && Skip_BAD_page) || (op == 'w' && ECC_ignore)) {
    fprintf(stderr, "Conflicting options, only one option at a time.\n\n");
    return 1;
  }

  title();

  if (programmer_type == PROGRAMMER_AUTO) {
    int types[SCRIBA_MAX_PROGRAMMERS];
    const char *descs[SCRIBA_MAX_PROGRAMMERS];
    int n = scriba_probe_available(types, descs, SCRIBA_MAX_PROGRAMMERS);

    if (n == 0) {
      fprintf(stderr, "No supported programmer device found!\n\n");
      return 1;
    }

    if (n > 1) {
      printf("Multiple programmers found:\n");
      for (int i = 0; i < n; i++)
        printf("  %d: %s\n", i + 1, descs[i]);
      printf("Select programmer (1-%d): ", n);
      fflush(stdout);

      char buf[16];
      int sel = 0;
      if (fgets(buf, sizeof(buf), stdin))
        sel = atoi(buf);

      if (sel < 1 || sel > n) {
        fprintf(stderr, "Invalid selection.\n");
        return 1;
      }
      programmer_type = types[sel - 1];
    } else {
      programmer_type = types[0];
    }
    SPI_CONTROLLER_Init(programmer_type);
  }

  if (scriba_init_programmer(programmer_type) < 0) {
    fprintf(stderr, "No supported programmer device found!\n\n");
    return 1;
  }

  if (scriba_detect_chip() < 0) {
    fprintf(stderr, "No flash chip detected!\n\n");
    goto shutdown;
  }

  snor_set_progress_callback(cli_progress_cb);

  flen = scriba_get_flash_size();

  if ((eepromsize || mw_eepromsize || seepromsize) && op == 'i') {
    fprintf(stderr, "Programmer not supported auto detect EEPROM!\n\n");
    goto shutdown;
  }

  if (spage_size) {
    if (!seepromsize) {
      fprintf(stderr, "Only use for SPI EEPROM!\n\n");
      goto shutdown;
    }
    if (((spage_size % 8) != 0) || (spage_size > (MAX_SEEP_PSIZE / 2))) {
      fprintf(stderr, "Invalid parameter %dB for page size SPI EEPROM!\n\n",
              spage_size);
      goto shutdown;
    }
    if (op == 'r')
      printf("Ignored set page size SPI EEPROM on READ.\n");
    else
      printf("Setting page size %dB for write.\n", spage_size);
  }

  if (OOB_size) {
    if (ECC_fcheck == 1) {
      printf("Ignore option -o, use with -d only!\n");
      OOB_size = 0;
    } else {
      if (OOB_size > 256) {
        fprintf(stderr, "Error: Maximum set OOB size <= 256!\n");
        goto shutdown;
      }
      if (OOB_size < 64) {
        fprintf(stderr, "Error: Minimum set OOB size >= 64!\n");
        goto shutdown;
      }
      printf("Set manual OOB size = %d.\n", OOB_size);
    }
  }

  switch (op) {
  case 'i':
    printf("Chip: %s (%ld bytes)\n", scriba_get_chip_name(), flen);
    exit_code = 0;
    break;

  case 'e': {
    printf("ERASE:\n");
    timer_start();
    int ret = op_erase(addr, len);
    timer_end();
    if (ret == 0)
      exit_code = 0;
    break;
  }

  case 'W': {
    timer_start();
    int ret = op_write_macro(op_arg, addr, len);
    timer_end();
    if (ret == 0)
      exit_code = 0;
    break;
  }

  case 'R': {
    timer_start();
    int ret = op_read_macro(op_arg, addr, len);
    timer_end();
    if (ret == 0)
      exit_code = 0;
    break;
  }

  case 'w': {
    printf("WRITE:\n");
    timer_start();
    int ret = op_write(op_arg, addr, len, vr);
    timer_end();
    if (ret == 0)
      exit_code = 0;
    break;
  }

  case 'r': {
    printf("READ:\n");
    timer_start();
    int ret = op_read(op_arg, addr, len);
    timer_end();
    if (ret == 0)
      exit_code = 0;
    break;
  }

  default:
    fprintf(stderr, "Unknown operation.\n");
    break;
  }

shutdown:
  scriba_shutdown();
  return exit_code;
}

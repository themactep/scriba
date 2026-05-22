/*
 * main.c
 * Copyright (C) 2018-2022 McMCC <mcmcc@mail.ru>
 * Copyright (C) 2025-2026 Paul Philippov <paul@themactep.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <time.h>

#ifndef GIT_COMMIT_DATE
#define GIT_COMMIT_DATE "unknown"
#endif
#ifndef GIT_COMMIT_HASH
#define GIT_COMMIT_HASH "unknown"
#endif

#include "flashcmd_api.h"
#include "ch341a_spi.h"
#include "ezp2019_spi.h"
#include "spi_controller.h"
#include "spi_nand_flash.h"

struct flash_cmd prog;
extern unsigned int bsize;

/* Global debug/trace flags */
int debug_enabled = 0;
int trace_enabled = 0;

#include "ch341a_i2c.h"
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

static int do_verify(const unsigned char *expected, unsigned long addr, unsigned long len)
{
	unsigned char *verify_buf = (unsigned char *)malloc(len);

	if (!verify_buf) {
		fprintf(stderr, "Malloc failed for verify buffer: len=%ld.\n", len);
		return 0;
	}

	if (prog.flash_read(verify_buf, addr, len) < 0) {
		fprintf(stderr, "Verify Read Status: BAD\n");
		free(verify_buf);
		return 0;
	}

	if (memcmp(verify_buf, expected, len) != 0) {
		fprintf(stderr, "Verify Status: BAD - Data mismatch\n");
		free(verify_buf);
		return 0;
	}

	free(verify_buf);
	return 1;
}

void title(void)
{
	const char *prog_name;
	if (programmer_type == PROGRAMMER_EZP2019)
		prog_name = "EZP2019";
	else if (programmer_type == PROGRAMMER_CH341A)
		prog_name = "CH341A";
	else
		prog_name = "Auto-detect";
	printf("Thingino %s Programming Tool v.%s-%s (based on SNANDer)\n", prog_name, GIT_COMMIT_DATE, GIT_COMMIT_HASH);
#ifdef CONFIG_STATIC
	printf("Static");
#else
	printf("Dynamic");
#endif
	printf(" build using libusb %s\n", get_libusb_version());
}

void usage(const char *program_name)
{
	char use[1024];
	snprintf(use, sizeof(use), "\nUsage: %s [options]\n"
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

int main(int argc, char *argv[])
{
 	int c, vr = 0, ret = 0;
 	char op = 0;
 	const char *op_arg = NULL;
 	unsigned char *buf = NULL;
 	int long long len = 0, addr = 0, flen = 0, wlen = 0;
 	FILE *fp;

	/* Long options */
	static struct option long_options[] = {
		{"debug", no_argument, NULL, 0},
		{"trace", no_argument, NULL, 0},
		{0, 0, 0, 0}
	};
	int option_index = 0;

	while ((c = getopt_long(argc, argv, "diIhveLkl:a:w:r:W:R:o:s:E:f:8P:", long_options, &option_index)) != -1)
	{
		if (c == 0)
		{
			/* Long option */
			const char *lname = long_options[option_index].name;
			if (strcmp(lname, "debug") == 0)
			{
				debug_enabled = 1;
				printf("Debug mode enabled\n");
				continue;
			}
			if (strcmp(lname, "trace") == 0)
			{
				trace_enabled = 1;
				debug_enabled = 1;
				printf("Trace mode enabled (debug forced on)\n");
				continue;
			}
		}
		switch (c)
		{
		case 'E':
			if ((eepromsize = parseEEPsize(optarg, &eeprom_info)) > 0)
			{
				memset(eepromname, 0, sizeof(eepromname));
				strncpy(eepromname, optarg, 10);
				if (len > eepromsize)
				{
					fprintf(stderr, "Error set size %lld, max size %d for EEPROM %s!\n", len, eepromsize, eepromname);
					exit(1);
				}
			}
			else if ((mw_eepromsize = deviceSize_3wire(optarg)) > 0)
			{
				memset(eepromname, 0, sizeof(eepromname));
				strncpy(eepromname, optarg, 10);
				org = 1;
				if (len > mw_eepromsize)
				{
					fprintf(stderr, "Error set size %lld, max size %d for EEPROM %s!\n", len, mw_eepromsize, eepromname);
					exit(1);
				}
			}
			else if ((seepromsize = parseSEEPsize(optarg, &seeprom_info)) > 0)
			{
				memset(eepromname, 0, sizeof(eepromname));
				strncpy(eepromname, optarg, 10);
				if (len > seepromsize)
				{
					fprintf(stderr, "Error set size %lld, max size %d for EEPROM %s!\n", len, seepromsize, eepromname);
					exit(1);
				}
			}
			else
			{
				fprintf(stderr, "Unknown EEPROM chip %s!\n", optarg);
				exit(1);
			}
			break;
		case '8':
			if (mw_eepromsize <= 0)
			{
				fprintf(stderr, "-8 option only for Microwire EEPROM chips!\n");
				exit(1);
			}
			org = 0;
			break;
		case 'f':
			if (mw_eepromsize <= 0)
			{
				fprintf(stderr, "-f option only for Microwire EEPROM chips!\n");
				exit(1);
			}
			fix_addr_len = strtoll(optarg, NULL, *optarg && *(optarg + 1) == 'x' ? 16 : 10);
			if (fix_addr_len > 32)
			{
				fprintf(stderr, "Address len is very big!\n");
				exit(1);
			}
			break;
		case 's':
			spage_size = strtoll(optarg, NULL, *optarg && *(optarg + 1) == 'x' ? 16 : 10);
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
			OOB_size = strtoll(optarg, NULL, *optarg && *(optarg + 1) == 'x' ? 16 : 10);
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
			if (!op)
			{
				op = c;
				op_arg = optarg;
			}
			else
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
				fprintf(stderr, "Unknown programmer type: %s (use ch341a, ezp2019, or auto)\n", optarg);
				exit(1);
			}
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

	if (op == 'x' || (ECC_ignore && !ECC_fcheck) || (ECC_ignore && Skip_BAD_page) || (op == 'w' && ECC_ignore))
	{
		fprintf(stderr, "Conflicting options, only one option at a time.\n\n");
		return 1;
	}

	if (programmer_type == PROGRAMMER_EZP2019) {
		if (ezp2019_spi_init() < 0) {
			fprintf(stderr, "EZP2019 programmer device not found!\n\n");
			return 1;
		}
	} else if (programmer_type == PROGRAMMER_CH341A) {
		if (ch341a_spi_init() < 0) {
			fprintf(stderr, "CH341A programmer device not found!\n\n");
			return 1;
		}
	} else {
		/* PROGRAMMER_AUTO: try EZP first, then CH341A */
		if (ezp2019_spi_init() == 0) {
			programmer_type = PROGRAMMER_EZP2019;
		} else if (ch341a_spi_init() == 0) {
			programmer_type = PROGRAMMER_CH341A;
		} else {
			fprintf(stderr, "No supported programmer device found!\n\n");
			return 1;
		}
	}

	title();

	if ((flen = flash_cmd_init(&prog)) <= 0)
		goto out;

	if ((eepromsize || mw_eepromsize || seepromsize) && op == 'i')
	{
		fprintf(stderr, "Programmer not supported auto detect EEPROM!\n\n");
		goto out;
	}

	if (spage_size)
	{
		if (!seepromsize)
		{
			fprintf(stderr, "Only use for SPI EEPROM!\n\n");
			goto out;
		}
		if (((spage_size % 8) != 0) || (spage_size > (MAX_SEEP_PSIZE / 2)))
		{
			fprintf(stderr, "Invalid parameter %dB for page size SPI EEPROM!\n\n", spage_size);
			goto out;
		}
		if (op == 'r')
			printf("Ignored set page size SPI EEPROM on READ.\n");
		else
			printf("Setting page size %dB for write.\n", spage_size);
	}

	if (OOB_size)
	{
		if (ECC_fcheck == 1)
		{
			printf("Ignore option -o, use with -d only!\n");
			OOB_size = 0;
		}
		else
		{
			if (OOB_size > 256)
			{
				fprintf(stderr, "Error: Maximum set OOB size <= 256!\n");
				goto out;
			}
			if (OOB_size < 64)
			{
				fprintf(stderr, "Error: Minimum set OOB size >= 64!\n");
				goto out;
			}
			printf("Set manual OOB size = %d.\n", OOB_size);
		}
	}

	if (op == 'e')
	{
		printf("ERASE:\n");
		if (addr && !len)
			len = flen - addr;
		else if (!addr && !len)
		{
			len = flen;
			printf("Set full erase chip!\n");
		}
		if (bsize > 0 && (len % bsize))
		{
			fprintf(stderr, "Please set len = 0x%016llX multiple of the block size 0x%08X\n", len, bsize);
			goto out;
		}
		printf("Erase addr = 0x%016llX, len = 0x%016llX\n", addr, len);
		ret = prog.flash_erase(addr, len);
		if (!ret)
		{
			printf("Status: OK\n");
			goto okout;
		}
		else
			printf("Status: BAD(%d)\n", ret);
		goto out;
	}

if (op == 'W')
 	{
 		printf("WRITE (Erase + Write + Verify):\n");

 		// Step 1: Erase
 		printf("Step 1/3 - ERASE:\n");
 		if (addr && !len)
 			len = flen - addr;
 		else if (!addr && !len)
 		{
 			len = flen;
 			printf("Set full erase chip!\n");
 		}
 		if (bsize > 0 && (len % bsize))
 		{
 			fprintf(stderr, "Please set len = 0x%016llX multiple of the block size 0x%08X\n", len, bsize);
 			goto out;
 		}
 		printf("Erase addr = 0x%016llX, len = 0x%016llX\n", addr, len);
 		ret = prog.flash_erase(addr, len);
 		if (ret)
 		{
 			printf("Erase Status: BAD(%d)\n", ret);
 			goto out;
 		}
 		printf("Erase Status: OK\n");

 		// Allocate buffer for write and verify operations
 		buf = (unsigned char *)malloc(len);
 		if (!buf)
 		{
 			fprintf(stderr, "Malloc failed for program buffer: len=%lld.\n", len);
 			goto out;
 		}

 		// Step 2: Write
 		printf("Step 2/3 - WRITE:\n");
 		fp = fopen(op_arg, "rb");
 		if (!fp)
 		{
 			fprintf(stderr, "Couldn't open file %s for reading.\n", op_arg);
 			free(buf);
 			goto out;
 		}
 		wlen = fread(buf, 1, len, fp);
 		if (ferror(fp))
 		{
 			fprintf(stderr, "Error reading file [%s]\n", op_arg);
 			fclose(fp);
 			free(buf);
 			goto out;
 		}

 		if (len == flen)
 			len = wlen;
 		printf("Write addr = 0x%016llX, len = 0x%016llX\n", addr, len);
 		ret = prog.flash_write(buf, addr, len);
 		if (ret <= 0)
 		{
 			printf("Write Status: BAD(%d)\n", ret);
 			fclose(fp);
 			free(buf);
 			goto out;
 		}
 		printf("Write Status: OK\n");

  		// Step 3: Verify
  		printf("Step 3/3 - VERIFY:\n");
  		if (!do_verify(buf, addr, len))
  		{
  			fprintf(stderr, "Write Status: FAILED\n");
  			fclose(fp);
  			free(buf);
  			goto out;
  		}
  		printf("Verify Status: OK\n");
  		fclose(fp);
  		free(buf);
  		goto okout;
 	}

if (op == 'R')
 	{
 		printf("READ (Read twice and compare):\n");

 		// Set up length and address
 		if (addr && !len)
 			len = flen - addr;
 		else if (!addr && !len)
 		{
 			len = flen;
 			printf("Set full chip check!\n");
 		}

 		// Allocate buffers for both reads
 		unsigned char *buf1 = (unsigned char *)malloc(len);
 		unsigned char *buf2 = (unsigned char *)malloc(len);
 		if (!buf1 || !buf2)
 		{
 			fprintf(stderr, "Malloc failed for check buffers: len=%lld.\n", len);
 			if (buf1) free(buf1);
 			if (buf2) free(buf2);
 			goto out;
 		}

 		// First read
 		printf("Step 1/2 - First READ:\n");
 		printf("Read addr = 0x%016llX, len = 0x%016llX\n", addr, len);
 		ret = prog.flash_read(buf1, addr, len);
 		if (ret < 0)
 		{
 			fprintf(stderr, "First Read Status: BAD(%d)\n", ret);
 			free(buf1);
 			free(buf2);
 			goto out;
 		}
 		printf("First Read Status: OK\n");

 		// Second read
 		printf("Step 2/2 - Second READ:\n");
 		printf("Read addr = 0x%016llX, len = 0x%016llX\n", addr, len);
 		ret = prog.flash_read(buf2, addr, len);
 		if (ret < 0)
 		{
 			fprintf(stderr, "Second Read Status: BAD(%d)\n", ret);
 			free(buf1);
 			free(buf2);
 			goto out;
 		}
 		printf("Second Read Status: OK\n");

 		if (memcmp(buf1, buf2, len) != 0)
 		{
 			long long mismatch_count = 0;
 			long long first_mismatch = -1;

 			for (long long i = 0; i < len; i++)
 			{
 				if (buf1[i] != buf2[i])
 				{
 					if (first_mismatch == -1)
 						first_mismatch = i;
 					mismatch_count++;
 				}
 			}

 			fprintf(stderr, "Compare Status: BAD - Found %lld mismatched bytes\n", mismatch_count);
 			fprintf(stderr, "First mismatch at address 0x%016llX (byte1=0x%02X, byte2=0x%02X)\n",
 					addr + first_mismatch, buf1[first_mismatch], buf2[first_mismatch]);
 			fprintf(stderr, "Read Status: FAILED - Flash may be unreliable\n");
 			free(buf1);
 			free(buf2);
 			goto out;
 		}
 		else
 		{
 			printf("Compare Status: OK - Both reads are identical\n");

 			fp = fopen(op_arg, "wb");
 			if (!fp)
 			{
 				fprintf(stderr, "Couldn't open file %s for writing.\n", op_arg);
 				free(buf1);
 				free(buf2);
 				goto out;
 			}
 			fwrite(buf1, 1, len, fp);
 			if (ferror(fp))
 			{
 				fprintf(stderr, "Error writing file [%s]\n", op_arg);
 				fclose(fp);
 				free(buf1);
 				free(buf2);
 				goto out;
 			}
 			fclose(fp);

 			printf("Read Status: OK - Verified data saved to %s\n", op_arg);
 			free(buf1);
 			free(buf2);
 			goto okout;
 		}
 	}

	if ((op == 'r') || (op == 'w'))
	{
		if (addr && !len)
			len = flen - addr;
		else if (!addr && !len)
			len = flen;
		buf = (unsigned char *)malloc(len);
		if (!buf)
		{
			fprintf(stderr, "Malloc failed for read buffer: len=%lld.\n", len);
			goto out;
		}
	}

	if (op == 'w')
	{
		printf("WRITE:\n");
		fp = fopen(op_arg, "rb");
		if (!fp)
		{
			fprintf(stderr, "Couldn't open file %s for reading.\n", op_arg);
			free(buf);
			goto out;
		}
		wlen = fread(buf, 1, len, fp);
		if (ferror(fp))
		{
			fprintf(stderr, "Error reading file [%s]\n", op_arg);
			fclose(fp);
			free(buf);
			goto out;
		}

		if (len == flen)
			len = wlen;
		printf("Write addr = 0x%016llX, len = 0x%016llX\n", addr, len);
		ret = prog.flash_write(buf, addr, len);
		if (ret > 0)
		{
			printf("Status: OK\n");
			if (vr)
			{
				printf("VERIFY:\n");
				if (!do_verify(buf, addr, len))
				{
					fprintf(stderr, "Status: BAD\n");
					fclose(fp);
					free(buf);
					goto out;
				}
				printf("Status: OK\n");
			}
		}
		else
			printf("Status: BAD(%d)\n", ret);
		fclose(fp);
		free(buf);
	}

	if (op == 'r')
	{
		printf("READ:\n");
		printf("Read addr = 0x%016llX, len = 0x%016llX\n", addr, len);
		ret = prog.flash_read(buf, addr, len);
		if (ret < 0)
		{
			fprintf(stderr, "Status: BAD(%d)\n", ret);
			free(buf);
			goto out;
		}
		fp = fopen(op_arg, "wb");
		if (!fp)
		{
			fprintf(stderr, "Couldn't open file %s for writing.\n", op_arg);
			free(buf);
			goto out;
		}
		fwrite(buf, 1, len, fp);
		if (ferror(fp))
		{
			fprintf(stderr, "Error writing file [%s]\n", op_arg);
			fclose(fp);
			free(buf);
			goto out;
		}
		fclose(fp);
		free(buf);
		printf("Status: OK\n");
		goto okout;
	}

out: // exit with errors
	if (programmer_type == PROGRAMMER_EZP2019)
		ezp2019_spi_shutdown();
	else
		ch341a_spi_shutdown();
	return 1;
okout: // exit without errors
	if (programmer_type == PROGRAMMER_EZP2019)
		ezp2019_spi_shutdown();
	else
		ch341a_spi_shutdown();
	return 0;
}

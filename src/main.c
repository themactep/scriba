/*
 * main.c
 * Copyright (C) 2018-2022 McMCC <mcmcc@mail.ru>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <time.h>

#include "flashcmd_api.h"
#include "ch341a_spi.h"
#include "spi_nand_flash.h"

struct flash_cmd prog;
extern unsigned int bsize;

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

void title(void)
{
	printf("Thingino CH341A Programming Tool v.%s-%s (based on SNANDer)\n", GIT_COMMIT_DATE, GIT_COMMIT_HASH);
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
				   "General:\n"
				   "  -h           Display help\n"
				   "  -L           List supported chips\n"
				   "  -i           Read chip ID\n"
				   "\n"
				   "Operations:\n"
				   "  -e           Erase chip\n"
				   "  -r <file>    Read chip to file\n"
				   "  -w <file>    Write file to chip\n"
				   "  -v           Verify after write\n"
				   "  -a <address> Set address\n"
				   "  -l <bytes>   Set length\n"
				   "SPI NAND:\n"
				   "  -d           Disable internal ECC\n"
				   "  -o <bytes>   Set OOB size\n"
				   "  -I           Ignore ECC errors\n"
				   "  -k           Skip BAD pages\n"
				   "EEPROM:\n"
				   "  -E <chip>    Select EEPROM type\n"
				   "  -8           Set 8-bit organization\n"
				   "  -f <bits>    Set address size\n"
				   "  -s <bytes>   Set page size\n",
		 program_name);
	printf(use);
	exit(0);
}

int main(int argc, char *argv[])
{
	int c, vr = 0, svr = 0, ret = 0;
	char *fname = NULL, op = 0;
	unsigned char *buf;
	int long long len = 0, addr = 0, flen = 0, wlen = 0;
	FILE *fp;

	title();

	while ((c = getopt(argc, argv, "diIhveLkl:a:w:r:o:s:E:f:8")) != -1)
	{
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
			if (!op)
			{
				op = c;
				fname = optarg;
			}
			else
				op = 'x';
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

	if (ch341a_spi_init() < 0)
	{
		fprintf(stderr, "Programmer device not found!\n\n");
		return 1;
	}

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

	if ((op == 'r') || (op == 'w'))
	{
		if (addr && !len)
			len = flen - addr;
		else if (!addr && !len)
		{
			len = flen;
		}
		// Allocate exactly len bytes, +1 is unnecessary unless null-terminating
		// (which isn't done here)
		buf = (unsigned char *)malloc(len);
		if (!buf)
		{
			fprintf(stderr, "Malloc failed for read buffer.\n");
			goto out;
		}
	}

	if (op == 'w')
	{
		printf("WRITE:\n");
		fp = fopen(fname, "rb");
		if (!fp)
		{
			fprintf(stderr, "Couldn't open file %s for reading.\n", fname);
			free(buf);
			goto out;
		}
		wlen = fread(buf, 1, len, fp);
		if (ferror(fp))
		{
			fprintf(stderr, "Error reading file [%s]\n", fname);
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
				op = 'r';
				svr = 1;
				printf("VERIFY:\n");
				goto very;
			}
		}
		else
			printf("Status: BAD(%d)\n", ret);
		fclose(fp);
		free(buf);
	}

very:
	if (op == 'r')
	{
		if (!svr)
			printf("READ:\n");
		else
			memset(buf, 0, len);
		printf("Read addr = 0x%016llX, len = 0x%016llX\n", addr, len);
		ret = prog.flash_read(buf, addr, len);
		if (ret < 0)
		{
			fprintf(stderr, "Status: BAD(%d)\n", ret);
			free(buf);
			goto out;
		}
		if (svr)
		{
			unsigned char ch1;
			int i = 0;

			fseek(fp, 0, SEEK_SET);
			ch1 = (unsigned char)getc(fp);

			while ((ch1 != EOF) && (i < len - 1) && (ch1 == buf[i++]))
				ch1 = (unsigned char)getc(fp);

			if (ch1 == buf[i])
			{
				printf("Status: OK\n");
				fclose(fp);
				free(buf);
				goto okout;
			}
			else
				fprintf(stderr, "Status: BAD\n");
			fclose(fp);
			free(buf);
			goto out;
		}
		fp = fopen(fname, "wb");
		if (!fp)
		{
			fprintf(stderr, "Couldn't open file %s for writing.\n", fname);
			free(buf);
			goto out;
		}
		fwrite(buf, 1, len, fp);
		if (ferror(fp))
		{
			fprintf(stderr, "Error writing file [%s]\n", fname);
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
	ch341a_spi_shutdown();
	return 1;
okout: // exit without errors
	ch341a_spi_shutdown();
	return 0;
}

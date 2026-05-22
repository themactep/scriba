/*
 * ezp2019_spi.c
 * USB driver for EZP2019/EZP2010/EZP2023 high-speed SPI programmers.
 * Based on reverse-engineered protocol from bokic/ezp2019.
 * Copyright (C) 2024 Paul Philippov <paul@themactep.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "ezp2019_spi.h"
#include <libusb-1.0/libusb.h>

/* SPI NAND opcodes (from spi_nand_flash_defs.h) */
#define _SPI_NAND_OP_READ_ID             0x9F
#define _SPI_NAND_OP_READ_ID_2           0x90
#define _SPI_NAND_OP_WRITE_ENABLE        0x06
#define _SPI_NAND_OP_WRITE_DISABLE       0x04
#define _SPI_NAND_OP_PROGRAM_LOAD_SINGLE 0x02
#define _SPI_NAND_OP_PROGRAM_LOAD_QUAD   0x32
#define _SPI_NAND_OP_BLOCK_ERASE         0xD8
#define _SPI_NAND_OP_RES                 0xAB

/* SPI NOR opcodes (from spi_nor_flash.h) */
#define OPCODE_RDSR      0x05
#define OPCODE_BE1       0xC7
#define OPCODE_BE        0x60
#define OPCODE_READ      0x03
#define OPCODE_FAST_READ 0x0B

/* Debug logging (compile-time, like _SPI_NAND_DEBUG_PRINTF) */
#ifdef EZP_DEBUG_ENABLED
#define EZP_DEBUG(fmt, ...) fprintf(stderr, "[DEBUG] " fmt, ##__VA_ARGS__)
#else
#define EZP_DEBUG(fmt, ...)
#endif

/* Global debug/trace flags from main.c */
extern int trace_enabled;

static void trace_dump(const char *label, const unsigned char *buf, unsigned int len)
{
	if (!trace_enabled || !len)
		return;
	fprintf(stderr, "[TRACE] %s (%u byte%s):", label, len, (len == 1) ? "" : "s");
	unsigned int i;
	for (i = 0; i < len; i++)
		fprintf(stderr, " %02x", buf[i]);
	fprintf(stderr, "\n");
}

static struct libusb_device_handle *ezp_handle = NULL;

/* Chip configuration from CONNECT probe */
static uint32_t ezp_chip_id = 0;
static uint32_t ezp_chip_size = 0;
static uint16_t ezp_chip_pagesize = 256;
static uint16_t ezp_chip_protocol = 0;        /* protocol_enum_cfg from EZP database, 0 for standard SPI */
static uint16_t ezp_chip_timeout = 1000;      /* timeout_retries from EZP database */
static bool ezp_chip_configured = false;
static bool ezp_chip_printed = false; /* only print chip ID once */

/* Write session cache: skip CONNECT+READ_SPI+trigger for consecutive pages */
static bool ezp_write_session = false;
static uint32_t ezp_write_next_addr = 0;

/* SPI emulation state */
#define EZP_CMD_BUF_SIZE 512
static uint8_t ezp_cmd_buf[EZP_CMD_BUF_SIZE];
static uint32_t ezp_cmd_len = 0;

/* ------------------------------------------------------------------ */

static void prepare_command_packet(uint8_t packet[EZP2019_PACKET_SIZE],
				    uint8_t command_id,
				    uint32_t size,
				    uint16_t pagesize,
				    uint16_t protocol,
				    uint16_t timeout,
				    uint32_t chip_id_val)
{
	memset(packet, 0, EZP2019_PACKET_SIZE);
	packet[1] = command_id;

	packet[2] = protocol & 0xFF;
	packet[3] = (protocol >> 8) & 0xFF;

	packet[4] = (pagesize >> 8) & 0xFF;
	packet[5] = pagesize & 0xFF;

	packet[6] = (timeout >> 8) & 0xFF;
	packet[7] = timeout & 0xFF;

	packet[8]  = (size >> 24) & 0xFF;
	packet[9]  = (size >> 16) & 0xFF;
	packet[10] = (size >> 8) & 0xFF;
	packet[11] = size & 0xFF;

	packet[12] = (chip_id_val >> 24) & 0xFF;
	packet[13] = (chip_id_val >> 16) & 0xFF;
	packet[14] = (chip_id_val >> 8) & 0xFF;
	packet[15] = chip_id_val & 0xFF;
}

static int ezp_send_raw_command(const uint8_t command[EZP2019_PACKET_SIZE],
				 uint8_t result[EZP2019_PACKET_SIZE])
{
	int sent = 0, received = 0, ret;

	if (ezp_handle == NULL)
		return -1;

	EZP_DEBUG("ezp_send_raw_command: cmd_id=0x%02x\n", command[1]);

	ret = libusb_bulk_transfer(ezp_handle, EZP_EP_CMD_OUT,
				    (unsigned char *)command, EZP2019_PACKET_SIZE,
				    &sent, EZP2019_USB_TIMEOUT);
	if (ret || sent != EZP2019_PACKET_SIZE) {
		fprintf(stderr, "EZP: failed to send command: %s\n", libusb_error_name(ret));
		return -1;
	}

	if (result) {
		usleep(50000);
		ret = libusb_bulk_transfer(ezp_handle, EZP_EP_IN,
					    result, EZP2019_PACKET_SIZE,
					    &received, EZP2019_USB_TIMEOUT);
		if (ret || received != EZP2019_PACKET_SIZE) {
			fprintf(stderr, "EZP: failed to read response: %s\n", libusb_error_name(ret));
			return -1;
		}
		trace_dump("EZP RESPONSE", result, EZP2019_PACKET_SIZE);
	}

	return 0;
}

static int ezp_reset_device(void)
{
	uint8_t packet[EZP2019_PACKET_SIZE];
	memset(packet, 0, EZP2019_PACKET_SIZE);
	packet[0] = 0x01;
	packet[1] = EZP_CMD_RESET;
	EZP_DEBUG("ezp_reset_device: sending RESET\n");
	return ezp_send_raw_command(packet, NULL);
}

static int ezp_do_connect(void)
{
	uint8_t packet[EZP2019_PACKET_SIZE];
	uint8_t result[EZP2019_PACKET_SIZE];
	int ret;

	/* If we already know the chip ID but haven't set a realistic size,
	 * set a reasonable default so the EZP hardware knows the flash geometry.
	 * The scriba NOR flash table detection runs after CONNECT, so we can't
	 * get the exact size yet during the first probe. */
	if (ezp_chip_id != 0 && ezp_chip_size == 0)
		ezp_chip_size = 16 * 1024 * 1024; /* 16 MB default */

	EZP_DEBUG("ezp_do_connect: probing chip (size=%u pagesize=%u proto=%u)\n",
		ezp_chip_size, ezp_chip_pagesize, ezp_chip_protocol);

	prepare_command_packet(packet, EZP_CMD_CONNECT,
				ezp_chip_size, ezp_chip_pagesize,
				ezp_chip_protocol, ezp_chip_timeout,
				ezp_chip_id);
	ret = ezp_send_raw_command(packet, result);
	if (ret < 0)
		return -1;

	/* Response bytes 1-3 contain the chip ID (24-bit big-endian) */
	ezp_chip_id = ((uint32_t)result[1] << 16) |
		       ((uint32_t)result[2] << 8) |
		        (uint32_t)result[3];
	ezp_chip_configured = true;

	if (!ezp_chip_printed) {
		fprintf(stderr, "[EZP] Detected chip ID: %02x %02x %02x\n",
			result[1], result[2], result[3]);
		ezp_chip_printed = true;
	}

	return 0;
}

static int ezp_poll_status(int max_retries, int sleep_us, const char *label)
{
	uint8_t spacket[EZP2019_PACKET_SIZE];
	uint8_t sresult[EZP2019_PACKET_SIZE];
	int total_polls = 0;
	int ret;

	EZP_DEBUG("ezp_poll_status: polling (max %d retries, %d us) for %s\n",
		max_retries, sleep_us, label);

	memset(spacket, 0, EZP2019_PACKET_SIZE);
	spacket[1] = EZP_CMD_STATUS;

	while (max_retries-- > 0) {
		total_polls++;
		usleep(sleep_us);
		memset(sresult, 0xFF, sizeof(sresult));
		ret = ezp_send_raw_command(spacket, sresult);
		if (ret == 0) {
			if ((sresult[0] & 0x01) == 0) {
				/* Match reference behaviour: skip first poll result */
				if (total_polls > 1) {
					EZP_DEBUG("%s: ready after %d polls\n",
						label, total_polls);
					return 0;
				}
			}
		}
	}

	fprintf(stderr, "EZP: %s timed out (%d polls, last_status=0x%02x)\n",
		label, total_polls, sresult[0]);
	return -1;
}

static int ezp_wait_ready(void)
{
	return ezp_poll_status(ezp_chip_timeout * 10, 50000, "ezp_wait_ready");
}

static void ezp_finalize_write_session(void)
{
	if (ezp_write_session) {
		usleep(100000);
		ezp_wait_ready();
		ezp_write_session = false;
	}
}

static int ezp_do_read(uint32_t addr, uint32_t len, uint8_t *buf)
{
	uint8_t packet[EZP2019_PACKET_SIZE];
	uint8_t result[EZP2019_PACKET_SIZE];
	uint8_t chunk_buf[EZP2019_READ_SIZE];
	int ret;

	EZP_DEBUG("ezp_do_read: addr=0x%08x len=%u\n", addr, len);

	/* Finalize any pending write stream before reading */
	ezp_finalize_write_session();

	/* Ensure chip is configured */
	if (!ezp_chip_configured) {
		ret = ezp_do_connect();
		if (ret < 0)
			return -1;
	}

	/* Send READ_SPI setup */
	memset(packet, 0, EZP2019_PACKET_SIZE);
	prepare_command_packet(packet, EZP_CMD_READ_SPI,
				ezp_chip_size, ezp_chip_pagesize,
				ezp_chip_protocol, ezp_chip_timeout,
				ezp_chip_id);
	ret = ezp_send_raw_command(packet, result);
	if (ret < 0)
		return -1;

	/* Send trigger transfer with address */
	memset(packet, 0, EZP2019_PACKET_SIZE);
	packet[1] = EZP_CMD_TRIGGER;
	packet[8]  = (addr >> 24) & 0xFF;
	packet[9]  = (addr >> 16) & 0xFF;
	packet[10] = (addr >> 8) & 0xFF;
	packet[11] = addr & 0xFF;
	ret = ezp_send_raw_command(packet, result);
	if (ret < 0)
		return -1;

	/* Read data from the device in EZP2019_READ_SIZE chunks.
	 * The EZP device sends data in fixed-size pages; we must request
	 * at least that many bytes or the transfer will overflow. */
	uint32_t remaining = len;
	uint32_t offset = 0;
	while (remaining > 0) {
		int chunk = (remaining > EZP2019_READ_SIZE) ? EZP2019_READ_SIZE : (int)remaining;
		int received = 0;

		/* Always request EZP2019_READ_SIZE bytes to avoid overflow,
		 * read into a temp buffer and copy only what we need. */
		if (remaining < EZP2019_READ_SIZE) {
			ret = libusb_bulk_transfer(ezp_handle, EZP_EP_IN,
						    chunk_buf, EZP2019_READ_SIZE,
						    &received, EZP2019_USB_TIMEOUT);
			if (ret == 0 && received >= chunk) {
				memcpy(buf + offset, chunk_buf, chunk);
				offset += chunk;
				remaining -= chunk;
			} else {
				fprintf(stderr, "EZP: read error at offset %u: %s (got %d bytes)\n",
					offset, libusb_error_name(ret), received);
				return -1;
			}
		} else {
			ret = libusb_bulk_transfer(ezp_handle, EZP_EP_IN,
						    buf + offset, chunk,
						    &received, EZP2019_USB_TIMEOUT);
			if (ret || received != chunk) {
				fprintf(stderr, "EZP: read error at offset %u: %s\n",
					offset, libusb_error_name(ret));
				return -1;
			}
			offset += received;
			remaining -= received;
		}
	}

	trace_dump("EZP READ DATA", buf, len);
	return 0;
}

static int ezp_do_write(uint32_t addr, uint32_t len, const uint8_t *data)
{
	uint8_t packet[EZP2019_PACKET_SIZE];
	uint8_t result[EZP2019_PACKET_SIZE];
	int ret;

	EZP_DEBUG("ezp_do_write: addr=0x%08x len=%u session=%d next=0x%08x\n",
		addr, len, ezp_write_session, ezp_write_next_addr);

	/* Full setup (CONNECT+READ_SPI+trigger) only on first write
	 * or when address is non-consecutive. Consecutive pages stream
	 * data directly to EP_DATA_OUT without re-triggering. */
	if (!ezp_write_session || addr != ezp_write_next_addr) {
		ret = ezp_do_connect();
		if (ret < 0) { ezp_write_session = false; return -1; }

		memset(packet, 0, EZP2019_PACKET_SIZE);
		prepare_command_packet(packet, EZP_CMD_READ_SPI,
					ezp_chip_size, ezp_chip_pagesize,
					ezp_chip_protocol, ezp_chip_timeout,
					ezp_chip_id);
		ret = ezp_send_raw_command(packet, result);
		if (ret < 0) { ezp_write_session = false; return -1; }

		/* Trigger sets the starting address for the write stream */
		memset(packet, 0, EZP2019_PACKET_SIZE);
		packet[1] = EZP_CMD_TRIGGER;
		packet[8]  = (addr >> 24) & 0xFF;
		packet[9]  = (addr >> 16) & 0xFF;
		packet[10] = (addr >> 8) & 0xFF;
		packet[11] = addr & 0xFF;
		ret = ezp_send_raw_command(packet, NULL);
		if (ret < 0) { ezp_write_session = false; return -1; }

		ezp_write_session = true;
		ezp_write_next_addr = addr;
	}

	/* Write data to DATA_OUT endpoint (fast path for consecutive pages) */
	int written = 0;
	ret = libusb_bulk_transfer(ezp_handle, EZP_EP_DATA_OUT,
				    (unsigned char *)data, (int)len,
				    &written, EZP2019_USB_TIMEOUT);
	if (ret || written != (int)len) {
		fprintf(stderr, "EZP: write error: %s\n", libusb_error_name(ret));
		ezp_write_session = false;
		return -1;
	}

	trace_dump("EZP WRITE DATA", data, len);

	ezp_write_next_addr = addr + len;
	return 0;
}

static int ezp_do_erase(void)
{
	uint8_t packet[EZP2019_PACKET_SIZE];
	uint8_t result[EZP2019_PACKET_SIZE];
	int ret;

	EZP_DEBUG("ezp_do_erase: chip erase\n");

	/* Finalize any pending write stream before erasing */
	ezp_finalize_write_session();

	/* Always connect before erase to ensure clean state */
	ret = ezp_do_connect();
	if (ret < 0)
		return -1;

	/* Send READ_SPI setup first */
	memset(packet, 0, EZP2019_PACKET_SIZE);
	prepare_command_packet(packet, EZP_CMD_READ_SPI,
				ezp_chip_size, ezp_chip_pagesize,
				ezp_chip_protocol, ezp_chip_timeout,
				ezp_chip_id);
	ret = ezp_send_raw_command(packet, result);
	if (ret < 0)
		return -1;

	/* Send erase command */
	memset(packet, 0, EZP2019_PACKET_SIZE);
	packet[0] = 0x01;
	packet[1] = EZP_CMD_ERASE;
	packet[0x1A] = 0x80;
	packet[0x1B] = 0x00;
	ret = ezp_send_raw_command(packet, NULL);
	if (ret < 0)
		return -1;

	/* Wait for completion with longer timeout for chip erase.
	 * Chip erase on a 16 MB flash can take 30-60 seconds. */
	usleep(5000);
	ret = ezp_poll_status(12000, 5000, "erase");
	if (ret < 0)
		return -1;

	return 0;
}

/* ------------------------------------------------------------------ */
/* SPI emulation layer */

int ezp_enable_pins(bool enable)
{
	EZP_DEBUG("ezp_enable_pins: %s\n", enable ? "CS_LOW" : "CS_HIGH");

	if (enable) {
		/* CS going low: start of new SPI transaction, clear buffer */
		ezp_cmd_len = 0;
	} else {
		/* CS going high: end of transaction, execute if write/erase */
		if (ezp_cmd_len > 0) {
			uint8_t opcode = ezp_cmd_buf[0];
			if (opcode == _SPI_NAND_OP_PROGRAM_LOAD_SINGLE ||
			    opcode == _SPI_NAND_OP_PROGRAM_LOAD_QUAD) {
				/* Page Program / Quad Page Program */
				uint32_t addr;
				int addr_offset;
				if (opcode == _SPI_NAND_OP_PROGRAM_LOAD_SINGLE) {
					/* Standard PP: opcode + 3 addr bytes */
					addr = ((uint32_t)ezp_cmd_buf[1] << 16) |
					       ((uint32_t)ezp_cmd_buf[2] << 8) |
					        (uint32_t)ezp_cmd_buf[3];
					addr_offset = 4;
				} else {
					/* QPP: opcode + 3 addr bytes */
					addr = ((uint32_t)ezp_cmd_buf[1] << 16) |
					       ((uint32_t)ezp_cmd_buf[2] << 8) |
					        (uint32_t)ezp_cmd_buf[3];
					addr_offset = 4;
				}
				uint32_t data_len = ezp_cmd_len - addr_offset;
				if (data_len > 0) {
					EZP_DEBUG("ezp: executing WRITE addr=0x%08x len=%u\n",
						addr, data_len);
					ezp_do_write(addr, data_len, ezp_cmd_buf + addr_offset);
				}
			} else if (opcode == 0x01 || opcode == 0x31 || opcode == 0x11) {
				/* Write status register - handled by EZP internally, no-op */
				EZP_DEBUG("ezp: WRSR (no-op)\n");
			} else if (opcode == _SPI_NAND_OP_READ_ID ||
				   opcode == _SPI_NAND_OP_READ_ID_2 ||
				   opcode == _SPI_NAND_OP_RES ||
				   opcode == OPCODE_RDSR ||
				   opcode == 0x35 || opcode == 0x15 ||
				   opcode == _SPI_NAND_OP_WRITE_ENABLE ||
				   opcode == _SPI_NAND_OP_WRITE_DISABLE) {
				/* Read/status/probe commands, handled during read phase, no-op here */
		} else if (opcode == OPCODE_BE1 || opcode == OPCODE_BE) {
			/* Bulk/chip erase */
			fprintf(stderr, "[EZP] Chip erase (opcode 0x%02x)\n", opcode);
			EZP_DEBUG("ezp: executing BULK ERASE\n");
			ezp_do_erase();
		} else if (opcode == _SPI_NAND_OP_BLOCK_ERASE ||
			   opcode == 0x20 || opcode == 0x40) {
			/* Sector/block erase - EZP only supports full chip erase.
			 * Extract address from buffer to report what was requested. */
			uint32_t er_addr = 0;
			if (ezp_cmd_len >= 4) {
				er_addr = ((uint32_t)ezp_cmd_buf[1] << 16) |
					  ((uint32_t)ezp_cmd_buf[2] << 8) |
					   (uint32_t)ezp_cmd_buf[3];
			}
			fprintf(stderr, "[EZP] Sector erase (opcode 0x%02x) at 0x%08x requested, "
				"EZP only supports full chip erase, erasing entire chip\n",
				opcode, er_addr);
			ezp_do_erase();
		} else {
			EZP_DEBUG("ezp: unknown opcode 0x%02x on CS high\n", opcode);
		}
		}
		ezp_cmd_len = 0;
	}
	return 0;
}

int ezp_config_stream(unsigned int speed)
{
	(void)speed;
	EZP_DEBUG("ezp_config_stream: speed=0x%x (no-op for EZP)\n", speed);
	return 0;
}

int ezp2019_spi_send_command(unsigned int writecnt, unsigned int readcnt,
			      const unsigned char *writearr, unsigned char *readarr)
{
	int ret;

	if (ezp_handle == NULL)
		return -1;

	trace_dump("SPI WRITE", writearr, writecnt);

	/* Accumulate write bytes */
	if (writecnt > 0) {
		if (ezp_cmd_len + writecnt > EZP_CMD_BUF_SIZE) {
			fprintf(stderr, "EZP: command buffer overflow (%u + %u > %u)\n",
				ezp_cmd_len, writecnt, EZP_CMD_BUF_SIZE);
			return -1;
		}
		memcpy(ezp_cmd_buf + ezp_cmd_len, writearr, writecnt);
		ezp_cmd_len += writecnt;
	}

	/* Handle read: analyze accumulated commands */
	if (readcnt > 0) {
		if (ezp_cmd_len == 0) {
			/* Read with no prior command - fill with 0xFF */
			memset(readarr, 0xFF, readcnt);
			return 0;
		}

		uint8_t opcode = ezp_cmd_buf[0];

		if (opcode == _SPI_NAND_OP_READ_ID) {
			/* JEDEC READ ID */
			EZP_DEBUG("ezp: RDID (0x9F) detected\n");
			ret = ezp_do_connect();
			if (ret < 0) {
				memset(readarr, 0xFF, readcnt);
				return -1;
			}
			/* Map EZP 24-bit chip_id to standard 5-byte JEDEC ID format:
			 * CONNECT returns response[1]=mfr, [2]=mem_type, [3]=capacity
			 * Standard JEDEC: buf[0]=mfr, buf[1]=mem_type, buf[2]=capacity, buf[3..4]=0 */
			memset(readarr, 0, readcnt);
			if (readcnt >= 1) readarr[0] = (ezp_chip_id >> 16) & 0xFF; /* mfr */
			if (readcnt >= 2) readarr[1] = (ezp_chip_id >> 8) & 0xFF;  /* mem type */
			if (readcnt >= 3) readarr[2] = ezp_chip_id & 0xFF;          /* capacity */
			/* buf[3], buf[4] stay 0 (from memset) */
			trace_dump("SPI READ (RDID)", readarr, readcnt);
		} else if (opcode == _SPI_NAND_OP_READ_ID_2) {
			/* READ Manufacturer/Device ID */
			EZP_DEBUG("ezp: READ_ID (0x90) detected\n");
			ret = ezp_do_connect();
			if (ret < 0) {
				memset(readarr, 0xFF, readcnt);
				return -1;
			}
			memset(readarr, 0, readcnt);
			if (readcnt >= 1) readarr[0] = (ezp_chip_id >> 16) & 0xFF;
			if (readcnt >= 2) readarr[1] = (ezp_chip_id >> 8) & 0xFF;
			trace_dump("SPI READ (READ_ID)", readarr, readcnt);
		} else if (opcode == OPCODE_RDSR || opcode == 0x35 || opcode == 0x15) {
			/* Read Status Register - return not-busy */
			memset(readarr, 0, readcnt);
			EZP_DEBUG("ezp: RDSR (0x%02x) returning 0x00\n", opcode);
			trace_dump("SPI READ (RDSR)", readarr, readcnt);
		} else if (opcode == OPCODE_READ || opcode == OPCODE_FAST_READ ||
			   opcode == 0x3B || opcode == 0x6B ||
			   opcode == 0xBB || opcode == 0xEB) {
			/* READ / FAST READ / Dual/Quad read */
			uint32_t addr;
			if (opcode == OPCODE_READ || opcode == OPCODE_FAST_READ) {
				if (ezp_cmd_len < 4) {
					fprintf(stderr, "EZP: READ with insufficient address bytes\n");
					return -1;
				}
				addr = ((uint32_t)ezp_cmd_buf[1] << 16) |
				       ((uint32_t)ezp_cmd_buf[2] << 8) |
				        (uint32_t)ezp_cmd_buf[3];
			} else {
				/* Dual/Quad - may have mode byte */
				addr = ((uint32_t)ezp_cmd_buf[1] << 16) |
				       ((uint32_t)ezp_cmd_buf[2] << 8) |
				        (uint32_t)ezp_cmd_buf[3];
			}
			EZP_DEBUG("ezp: READ (0x%02x) addr=0x%08x len=%u\n",
				opcode, addr, readcnt);
			ret = ezp_do_read(addr, readcnt, readarr);
			if (ret < 0)
				return -1;
			trace_dump("SPI READ (DATA)", readarr, readcnt);
		} else if (opcode == _SPI_NAND_OP_RES) {
			/* Read Electronic Signature */
			memset(readarr, 0, readcnt);
			EZP_DEBUG("ezp: RES (0xAB) returning 0x00\n");
		} else {
			fprintf(stderr, "EZP: unhandled read opcode 0x%02x (cmd_len=%u readcnt=%u)\n",
				opcode, ezp_cmd_len, readcnt);
			memset(readarr, 0xFF, readcnt);
		}

		/* Clear buffer after read operation */
		ezp_cmd_len = 0;
	}

	return 0;
}

/* ------------------------------------------------------------------ */
/* Init / shutdown */

int ezp2019_spi_shutdown(void)
{
	EZP_DEBUG("ezp2019_spi_shutdown: shutting down EZP2019\n");

	if (ezp_handle == NULL)
		return 0;

	/* Finalize any pending write stream */
	ezp_finalize_write_session();

	ezp_reset_device();

	libusb_release_interface(ezp_handle, 0);
	libusb_close(ezp_handle);
	libusb_exit(NULL);
	ezp_handle = NULL;

	ezp_chip_configured = false;
	ezp_chip_id = 0;

	EZP_DEBUG("ezp2019_spi_shutdown: shutdown complete\n");
	return 0;
}

int ezp2019_spi_init(void)
{
	int ret;
	struct libusb_device_descriptor desc;

	EZP_DEBUG("ezp2019_spi_init: initializing EZP2019\n");

	ret = libusb_init(NULL);
	if (ret < 0) {
		fprintf(stderr, "EZP: could not initialize libusb\n");
		return -1;
	}

#ifdef EZP_DEBUG_ENABLED
#if LIBUSB_API_VERSION >= 0x01000106
	libusb_set_option(NULL, LIBUSB_OPTION_LOG_LEVEL, 3);
#else
	libusb_set_debug(NULL, 3);
#endif
#endif

	/* Try each known EZP PID */
	static const uint16_t ezp_pids[] = {
		EZP2019_PID,
		EZP2019_PLUS_PID,
		EZP2023_PID,
		0
	};
	const uint16_t *pid = ezp_pids;
	while (*pid) {
		ezp_handle = libusb_open_device_with_vid_pid(NULL, EZP2019_VID, *pid);
		if (ezp_handle != NULL)
			break;
		pid++;
	}

	if (ezp_handle == NULL) {
		fprintf(stderr, "EZP: could not open device %04x:",
			EZP2019_VID);
		for (pid = ezp_pids; *pid; pid++)
			fprintf(stderr, "%s%04x", (pid == ezp_pids) ? "" : ", ", *pid);
		fprintf(stderr, "\n");
		fprintf(stderr, "Check: 1) device is connected  2) run with sudo "
			"or install 40-persistent-ezp2019.rules\n");
		/* do NOT libusb_exit here — caller may try CH341A next */
		libusb_exit(NULL);
		return -1;
	}

	printf("Found programmer device: www.zhifengsoft.com - EZP2019\n");

	/* Set auto-detach so libusb handles kernel driver automatically */
	ret = libusb_set_auto_detach_kernel_driver(ezp_handle, 1);
	if (ret != 0 && ret != LIBUSB_ERROR_NOT_SUPPORTED) {
		EZP_DEBUG("ezp2019_spi_init: auto_detach not supported: %s\n",
			libusb_error_name(ret));
	}

	/* Read string descriptors — some EZP models require this before
	 * they will accept commands on the bulk endpoints. */
	{
		uint8_t tmp[256];
		libusb_control_transfer(ezp_handle,
			LIBUSB_ENDPOINT_IN, LIBUSB_REQUEST_GET_DESCRIPTOR,
			(LIBUSB_DT_STRING << 8) | 0x01, 0x0409,
			tmp, sizeof(tmp), 1000);
		libusb_control_transfer(ezp_handle,
			LIBUSB_ENDPOINT_IN, LIBUSB_REQUEST_GET_DESCRIPTOR,
			(LIBUSB_DT_STRING << 8) | 0x02, 0x0409,
			tmp, sizeof(tmp), 1000);
	}

	ret = libusb_claim_interface(ezp_handle, 0);
	if (ret != 0) {
		fprintf(stderr, "EZP: failed to claim interface 0: %s\n",
			libusb_error_name(ret));
		libusb_close(ezp_handle);
		libusb_exit(NULL);
		ezp_handle = NULL;
		return -1;
	}

	struct libusb_device *dev = libusb_get_device(ezp_handle);
	if (dev) {
		ret = libusb_get_device_descriptor(dev, &desc);
		if (ret == 0) {
			printf("Device revision is %d.%01d.%01d\n",
			       (desc.bcdDevice >> 8) & 0x00FF,
			       (desc.bcdDevice >> 4) & 0x000F,
			       (desc.bcdDevice >> 0) & 0x000F);
		}
	}

	/* Do NOT reset here — earlier EZP models may not support it
	 * and will STALL the endpoint. RESET is sent before each
	 * read/write/erase operation instead. */

	/* Initialize state */
	ezp_cmd_len = 0;
	ezp_chip_configured = false;
	ezp_chip_id = 0;
	ezp_chip_size = 0;
	ezp_chip_pagesize = 256;
	ezp_chip_protocol = 0;
	ezp_chip_timeout = 1000;

	EZP_DEBUG("ezp2019_spi_init: initialization complete\n");

	return 0;
}

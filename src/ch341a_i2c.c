/*
 * ch341a_i2c.c
 * Copyright 2011 asbokid <ballymunboy@gmail.com>
 * Programming tool for the 24Cxx serial EEPROMs using the Winchiphead CH341A IC
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <libusb-1.0/libusb.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "ch341a_i2c.h"

/* Global debug flag from main.c */
extern int debug_enabled;

/* Debug printf - now controlled by global debug_enabled flag */
#define dprintf(args...) do { if (debug_enabled) fprintf(stderr, "[DEBUG] " args); } while(0)

extern struct libusb_device_handle *handle;
unsigned char *readbuf;
uint32_t getnextpkt; // set by the callback function
uint32_t syncackpkt; // synch / ack flag used by BULK OUT cb function
uint32_t byteoffset;

// callback functions for async USB transfers
static void cbBulkIn(struct libusb_transfer *transfer);
static void cbBulkOut(struct libusb_transfer *transfer);

void ch341ReadCmdMarshall(uint8_t *buffer, uint32_t addr, struct EEPROM *eeprom_info)
{
	uint8_t *ptr = buffer;
	uint8_t msb_addr;
	uint32_t size_kb;

	*ptr++ = mCH341A_CMD_I2C_STREAM;  // 0
	*ptr++ = mCH341A_CMD_I2C_STM_STA; // 1
	// Write address
	*ptr++ = mCH341A_CMD_I2C_STM_OUT | ((*eeprom_info).addr_size + 1); // 2: I2C bus adddress + EEPROM address
	if ((*eeprom_info).addr_size >= 2)
	{
		// 24C32 and more
		msb_addr = addr >> 16 & (*eeprom_info).i2c_addr_mask;
		*ptr++ = (EEPROM_I2C_BUS_ADDRESS | msb_addr) << 1; // 3
		*ptr++ = (addr >> 8 & 0xFF);			   // 4
		*ptr++ = (addr >> 0 & 0xFF);			   // 5
	}
	else
	{
		// 24C16 and less
		msb_addr = addr >> 8 & (*eeprom_info).i2c_addr_mask;
		*ptr++ = (EEPROM_I2C_BUS_ADDRESS | msb_addr) << 1; // 3
		*ptr++ = (addr >> 0 & 0xFF);			   // 4
	}
	// Read
	*ptr++ = mCH341A_CMD_I2C_STM_STA;			 // 6/5
	*ptr++ = mCH341A_CMD_I2C_STM_OUT | 1;			 // 7/6
	*ptr++ = ((EEPROM_I2C_BUS_ADDRESS | msb_addr) << 1) | 1; // 8/7: Read command

	static const unsigned char i2c_cfg_tail[] = {
		0x00, 0x06, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	static const unsigned char i2c_cfg_mid[] = {
		0x00, 0x00, 0x11, 0x4d, 0x40, 0x77, 0xcd, 0xab, 0xba, 0xdc
	};
	static const unsigned char i2c_frame2[] = {
		0xe0, 0x00, 0x00, 0xc4, 0xf1, 0x12, 0x00, 0x11,
		0x4d, 0x40, 0x77, 0xf0, 0xf1, 0x12, 0x00,
		0xd9, 0x8b, 0x41, 0x7e, 0x00, 0xe0, 0xfd, 0x7f,
		0xf0, 0xf1, 0x12, 0x00, 0x5a, 0x88, 0x41, 0x7e
	};
	static const unsigned char i2c_frame3[] = {
		0xe0, 0x00, 0x00, 0x2a, 0x88, 0x41, 0x7e, 0x06,
		0x04, 0x00, 0x00, 0x11, 0x4d, 0x40, 0x77,
		0xe8, 0xf3, 0x12, 0x00, 0x14, 0x00, 0x00, 0x00,
		0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};

	/* Configuration */
	*ptr++ = 0xE0;
	*ptr++ = 0x00;
	if ((*eeprom_info).addr_size < 2)
		*ptr++ = 0x10;
	memcpy(ptr, i2c_cfg_tail, sizeof(i2c_cfg_tail));
	ptr += sizeof(i2c_cfg_tail);
	size_kb = (*eeprom_info).size / 1024;
	*ptr++ = size_kb & 0xFF;
	*ptr++ = (size_kb >> 8) & 0xFF;
	memcpy(ptr, i2c_cfg_mid, sizeof(i2c_cfg_mid));
	ptr += sizeof(i2c_cfg_mid);

	/* Frame 2 */
	*ptr++ = mCH341A_CMD_I2C_STREAM;
	memcpy(ptr, i2c_frame2, sizeof(i2c_frame2));
	ptr += sizeof(i2c_frame2);

	/* Frame 3 */
	*ptr++ = mCH341A_CMD_I2C_STREAM;
	memcpy(ptr, i2c_frame3, sizeof(i2c_frame3));
	ptr += sizeof(i2c_frame3);

	// Finalize
	*ptr++ = mCH341A_CMD_I2C_STREAM;  // 0xAA
	*ptr++ = 0xDF;			  // ???
	*ptr++ = mCH341A_CMD_I2C_STM_IN;  // 0xC0
	*ptr++ = mCH341A_CMD_I2C_STM_STO; // 0x75
	*ptr++ = mCH341A_CMD_I2C_STM_END; // 0x00

	assert(ptr - buffer == CH341_EEPROM_READ_CMD_SZ);
}

// Read N bytes from device (in packets of 32 bytes)
int32_t ch341readEEPROM(uint8_t *buffer, uint32_t bytestoread, struct EEPROM *eeprom_info)
{
	uint8_t ch341outBuffer[EEPROM_READ_BULKOUT_BUF_SZ];
	uint8_t ch341inBuffer[IN_BUF_SZ]; // 0x100 bytes
	int32_t ret = 0, readpktcount = 0;
	struct libusb_transfer *xferBulkIn, *xferBulkOut;
	struct timeval tv = {0, 100}; // our async polling interval

	dprintf("ch341readEEPROM: reading %u bytes\n", bytestoread);

	xferBulkIn = libusb_alloc_transfer(0);
	xferBulkOut = libusb_alloc_transfer(0);

	if (!xferBulkIn || !xferBulkOut)
	{
		fprintf(stderr, "Couldn't allocate USB transfer structures\n"); // Use stderr
		if (debug_enabled)
			fprintf(stderr, "[DEBUG] ch341readEEPROM: transfer allocation failed\n");
		return -1;
	}

	byteoffset = 0;

	dprintf("ch341readEEPROM: allocated USB transfer structures\n");

	memset(ch341inBuffer, 0, EEPROM_READ_BULKIN_BUF_SZ);
	ch341ReadCmdMarshall(ch341outBuffer, 0, eeprom_info); // Fill output buffer

	libusb_fill_bulk_transfer(xferBulkIn, handle, BULK_READ_ENDPOINT, ch341inBuffer,
				  EEPROM_READ_BULKIN_BUF_SZ, cbBulkIn, NULL, DEFAULT_TIMEOUT);

	libusb_fill_bulk_transfer(xferBulkOut, handle, BULK_WRITE_ENDPOINT,
				  ch341outBuffer, EEPROM_READ_BULKOUT_BUF_SZ, cbBulkOut, NULL, DEFAULT_TIMEOUT);

	dprintf("ch341readEEPROM: filled USB transfer structures\n");

	ret = libusb_submit_transfer(xferBulkIn);
	if (ret < 0)
		dprintf("ch341readEEPROM: BULK IN submit failed with error %d: %s\n", ret, libusb_error_name(ret));
	else
		dprintf("ch341readEEPROM: submitted BULK IN start packet\n");

	ret = libusb_submit_transfer(xferBulkOut);
	if (ret < 0)
		dprintf("ch341readEEPROM: BULK OUT submit failed with error %d: %s\n", ret, libusb_error_name(ret));
	else
		dprintf("ch341readEEPROM: submitted BULK OUT setup packet\n");

	readbuf = buffer;

	while (1)
	{
		printf("Read %d%% [%d] of [%d] bytes      ", 100 * byteoffset / bytestoread, byteoffset, bytestoread);
		printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");
		fflush(stdout);
		ret = libusb_handle_events_timeout(NULL, &tv);

			if (ret < 0 || (int32_t)getnextpkt == -1)
		{								       // indicates an error
			fprintf(stderr, "ret from libusb_handle_timeout = %d\n", ret); // Use stderr
			fprintf(stderr, "getnextpkt = %d\n", getnextpkt);	       // Use stderr
			if (ret < 0)
			{
				fprintf(stderr, "USB read error : %s\n", strerror(-ret)); // Use stderr
				if (debug_enabled)
					fprintf(stderr, "[DEBUG] ch341readEEPROM: libusb_handle_events_timeout error %d\n", ret);
			}
			if (debug_enabled)
				fprintf(stderr, "[DEBUG] ch341readEEPROM: read operation failed, aborting\n");
			libusb_free_transfer(xferBulkIn);
			libusb_free_transfer(xferBulkOut);
			return -1;
		}
		if (getnextpkt == 1)
		{			// callback function reports a new BULK IN packet received
			getnextpkt = 0; // reset the flag
			readpktcount++; // increment the read packet counter
			byteoffset += EEPROM_READ_BULKIN_BUF_SZ;
			if (byteoffset == bytestoread)
				break;

			dprintf("\nRe-submitting transfer request to BULK IN endpoint\n");
			libusb_submit_transfer(xferBulkIn); // re-submit request for next BULK IN packet of EEPROM data
			if (syncackpkt)
				syncackpkt = 0;
			// if 4th packet received, we are at end of 0x80 byte data block,
			// if it is not the last block, then resubmit request for data
			if (readpktcount == 4)
			{
				dprintf("\nSubmitting next transfer request to BULK OUT endpoint\n");
				readpktcount = 0;

				ch341ReadCmdMarshall(ch341outBuffer, byteoffset, eeprom_info); // Fill output buffer
				libusb_fill_bulk_transfer(xferBulkOut, handle, BULK_WRITE_ENDPOINT, ch341outBuffer,
							  EEPROM_READ_BULKOUT_BUF_SZ, cbBulkOut, NULL, DEFAULT_TIMEOUT);

				libusb_submit_transfer(xferBulkOut); // update transfer struct (with new EEPROM page offset)
								     // and re-submit next transfer request to BULK OUT endpoint
			}
		}
	}
	printf("Read 100%% [%d] of [%d] bytes      \n", byteoffset, bytestoread);

	if (debug_enabled)
		fprintf(stderr, "[DEBUG] ch341readEEPROM: read completed successfully\n");

	libusb_free_transfer(xferBulkIn);
	libusb_free_transfer(xferBulkOut);
	return 0;
}

// Callback function for async bulk in comms
void cbBulkIn(struct libusb_transfer *transfer)
{
	int i;

	switch (transfer->status)
	{
	case LIBUSB_TRANSFER_COMPLETED:
		// display the contents of the BULK IN data buffer
		dprintf("cbBulkIn(): status %d - Read %d bytes\n", transfer->status, transfer->actual_length);

		for (i = 0; i < transfer->actual_length; i++)
		{
				if (!(i % 16)) {
					dprintf("\n   ");
				}
			dprintf("%02x ", transfer->buffer[i]);
		}
		dprintf("\n");
		// copy read data to our EEPROM buffer
		memcpy(readbuf + byteoffset, transfer->buffer, transfer->actual_length);
		getnextpkt = 1;
		break;
	default:
		fprintf(stderr, "\ncbBulkIn: error : %d\n", transfer->status); // Use stderr
		if (debug_enabled)
			fprintf(stderr, "[DEBUG] cbBulkIn: transfer failed with status %d (%s)\n",
				transfer->status, libusb_error_name(transfer->status));
		getnextpkt = -1;
	}
	return;
}

// Callback function for async bulk out comms
void cbBulkOut(struct libusb_transfer *transfer __attribute__((unused)))
{
	syncackpkt = 1;
	dprintf("cbBulkOut(): Sync/Ack received: status %d\n", transfer->status);
	if (transfer->status != LIBUSB_TRANSFER_COMPLETED && debug_enabled)
		fprintf(stderr, "[DEBUG] cbBulkOut: transfer completed with non-success status %d (%s)\n",
			transfer->status, libusb_error_name(transfer->status));
	return;
}

// Write N bytes to 24c32/24c64 device (in packets of 32 bytes)
int32_t ch341writeEEPROM(uint8_t *buffer __attribute__((unused)), uint32_t bytesum, struct EEPROM *eeprom_info __attribute__((unused)))
{
	uint32_t bytes = bytesum;

	// Implementation would go here

	printf("Written 100%% [%d] of [%d] bytes      \n", bytesum - bytes, bytesum);
	return 0;
}

// Passed an EEPROM name (case-sensitive), returns its byte size
int32_t parseEEPsize(char *eepromname, struct EEPROM *eeprom)
{
	int i;

	for (i = 0; eepromlist[i].size; i++)
	{
		if (strstr(eepromlist[i].name, eepromname))
		{
			memcpy(eeprom, &(eepromlist[i]), sizeof(struct EEPROM));
			return (eepromlist[i].size);
		}
	}

	return -1;
}

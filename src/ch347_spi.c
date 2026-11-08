/**
 * @file ch347_spi.c
 * @brief WCH CH347 USB-to-SPI bridge (CH347T in mode 1, CH347F)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The CH347 is a USB 2.0 high-speed bridge with a hardware SPI master, so
 * unlike the CH341A there is no bit swapping and no pin bit-banging: every
 * operation is one framed command on a pair of bulk endpoints.
 *
 * Frame format (both directions): [cmd, len_lo, len_hi, data...]
 *
 *   0xC0  SPI config    OUT: 26-byte config     IN: [C0 01 00 status]
 *   0xC1  CS control    OUT: 10 bytes, no reply
 *   0xC2  SPI write+read OUT: data              IN: [C2 len data] frames,
 *                                                    the bytes clocked in
 *   0xC3  SPI read      OUT: u32 LE byte count  IN: [C3 len data] frames
 *   0xC4  SPI write     OUT: data               IN: [C4 01 00 status]
 *   0xCA  info read     OUT: [00] = chip info   IN: [CA len ver_lo ver_hi ...]
 *
 * Protocol references: flashrom programmers/ch347_spi.c (Nicholas Chin),
 * WCH's ch34x_mphsi_master_linux driver, and
 * https://github.com/nic3-14159/CH347-Research (USB captures).
 *
 * Frame sizes differ between firmware revisions: the 2022 firmware never sends
 * more than 510 bytes per frame, while WCH's newer Linux driver reads 4 KB
 * frames. Writes therefore use 510-byte frames, which every revision
 * accepts, and replies are parsed from one ordered byte stream, so either
 * framing works and a transfer carrying several replies loses nothing.
 *
 * Every USB transfer is a round trip (an especially costly one under
 * WebUSB), and the flash layers issue a command as several small calls:
 * opcode, address and data are separate writes. So writes made while CS is
 * low are held back and sent as one frame when CS goes high, when a read
 * follows, or when the frame is full. A short read that follows held writes
 * (a status-register or ID read) goes out as one 0xC2 frame carrying both.
 *
 * Chip-select flags: firmware that supports it (CH347T 0x0341 or newer, any
 * CH347F, the same gate WCH's Linux driver applies for its default "medium
 * speed" mode) takes CS control inside a 0xC2 frame, in the top bits of the
 * length high byte: bit 7 asserts CS0 before the frame, bit 6 releases it
 * after. With that, cs_select() is deferred to the first frame and a write
 * command ending at cs_deselect() is one frame with both flags, so write
 * enable and page program each cost one round trip instead of three. Only a
 * transaction that ends with a read still needs a separate 0xC1 to release
 * CS. The mode can be turned off with SPI_OPT_NO_CS_FLAGS.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <libusb-1.0/libusb.h>

#include "ch347_spi.h"
#include "spi_controller.h"

extern int debug_enabled;
extern int trace_enabled;

#define CH347_EP_OUT      0x06
#define CH347_EP_IN       0x86
#define CH347_EP_SIZE     512   /* high-speed bulk wMaxPacketSize */

#define CH347_CMD_SPI_SET_CFG 0xC0
#define CH347_CMD_SPI_CS_CTRL 0xC1
#define CH347_CMD_SPI_OUT_IN  0xC2
#define CH347_CMD_SPI_IN      0xC3
#define CH347_CMD_SPI_OUT     0xC4
#define CH347_CMD_INFO_RD     0xCA

#define CH347_HDR_LEN     3
#define CH347_OUT_FRAME   510   /* largest OUT frame every firmware accepts */
#define CH347_OUT_PAYLOAD (CH347_OUT_FRAME - CH347_HDR_LEN)
#define CH347_IN_MAX      4096  /* largest single IN request */
#define CH347_LEN_HI_MASK 0x0F  /* length high byte; bits 7..4 carry CS flags */

/* CS control byte for the first chip select (the one programmer boards wire
 * to the flash socket): bit 7 = apply this field, bit 6 = deassert. */
#define CH347_CS_ASSERT   0x80
#define CH347_CS_DEASSERT 0xC0

/* CS0 flags in a 0xC2 frame's length high byte (chip-select-flag mode) */
#define CH347_C2_CS0_START 0x80   /* assert CS0 before the frame */
#define CH347_C2_CS0_STOP  0x40   /* release CS0 after the frame */
#define CH347T_CS_FLAGS_MIN_FW 0x0341

#define CH347_USB_TIMEOUT 1000  /* ms, per USB transfer */
#define CH347_DEFAULT_DIV 2     /* 60 MHz >> 2 = 15 MHz, flashrom's default */

#define DBG(...) do { if (debug_enabled) fprintf(stderr, "[DEBUG] " __VA_ARGS__); } while (0)

static struct libusb_device_handle *ch347_handle = NULL;
static int ch347_iface = -1;
static bool ch347_is_f = false;          /* CH347F (vs CH347T) */
static unsigned int ch347_divisor = CH347_DEFAULT_DIV;

/* Writes held back until CS goes high, a read follows, or a frame is full */
static uint8_t ch347_pend[CH347_OUT_PAYLOAD];
static unsigned int ch347_pend_len = 0;

/* Chip-select-flag mode, and in it a cs_select() not yet sent */
static bool ch347_csflags = false;
static bool ch347_cs_start = false;

/* Reply stream: bytes received but not yet consumed */
static uint8_t ch347_ib[CH347_IN_MAX];
static unsigned int ch347_ib_pos = 0;
static unsigned int ch347_ib_len = 0;

/* Set by any USB error and cleared only by init/reinit. A held write that
 * fails is reported by cs_deselect, whose result the flash layers often
 * ignore; failing every later call stops the operation at its next
 * command instead of letting it carry on as if the write had landed. */
static bool ch347_failed = false;

static const struct {
	uint16_t pid;
	int iface;
	bool is_f;
	const char *name;
} ch347_models[] = {
	{ CH347T_PID, CH347T_SPI_IFACE, false, "CH347T" },
	{ CH347F_PID, CH347F_SPI_IFACE, true,  "CH347F" },
};

static unsigned int ch347_divisor_for_speed(void);

static void trace_dump(const char *label, const unsigned char *buf, unsigned int len)
{
	if (!trace_enabled || !len)
		return;
	fprintf(stderr, "[TRACE] %s (%u byte%s):", label, len, (len == 1) ? "" : "s");
	for (unsigned int i = 0; i < len; i++)
		fprintf(stderr, " %02x", buf[i]);
	fprintf(stderr, "\n");
}

/* ------------------------------------------------------------------ */
/* USB plumbing */

static int ch347_bulk_out(const uint8_t *buf, int len)
{
	int sent = 0;
	int ret = libusb_bulk_transfer(ch347_handle, CH347_EP_OUT, (unsigned char *)buf,
				       len, &sent, CH347_USB_TIMEOUT);
	if (ret < 0 || sent != len) {
		fprintf(stderr, "CH347: bulk OUT of %d bytes failed: %s (sent %d)\n",
			len, ret < 0 ? libusb_error_name(ret) : "short write", sent);
		return -1;
	}
	return 0;
}

static int ch347_bulk_in(uint8_t *buf, int len, int *got)
{
	*got = 0;
	int ret = libusb_bulk_transfer(ch347_handle, CH347_EP_IN, buf, len, got,
				       CH347_USB_TIMEOUT);
	if (ret < 0) {
		fprintf(stderr, "CH347: bulk IN of %d bytes failed: %s\n",
			len, libusb_error_name(ret));
		return -1;
	}
	return 0;
}

static void ch347_in_reset(void)
{
	ch347_ib_pos = 0;
	ch347_ib_len = 0;
}

/* Take n bytes from the reply stream into dst (NULL discards them). `owed`
 * is a lower bound on the bytes the device has yet to send for the reply
 * being read, at least n. A request of `owed` rounded down to whole packets
 * can neither stall nor overflow (a request that is not a multiple of the
 * packet size overflows when a full packet arrives); with less than a packet
 * owed, one packet is asked for, which the device ends with a short packet.
 * Bytes beyond the reply stay buffered for the next one. */
static int ch347_in_take(uint8_t *dst, unsigned int n, unsigned int owed)
{
	if (owed < n)
		owed = n;
	while (n) {
		if (ch347_ib_pos == ch347_ib_len) {
			int want = owed >= CH347_EP_SIZE
				? (int)(owed / CH347_EP_SIZE) * CH347_EP_SIZE : CH347_EP_SIZE;
			int got;

			if (want > CH347_IN_MAX)
				want = CH347_IN_MAX;
			if (ch347_bulk_in(ch347_ib, want, &got) < 0)
				return -1;
			if (got == 0) {
				fprintf(stderr, "CH347: empty reply\n");
				return -1;
			}
			ch347_ib_pos = 0;
			ch347_ib_len = (unsigned int)got;
		}

		unsigned int k = ch347_ib_len - ch347_ib_pos;
		if (k > n)
			k = n;
		if (dst) {
			memcpy(dst, ch347_ib + ch347_ib_pos, k);
			dst += k;
		}
		ch347_ib_pos += k;
		n -= k;
		owed -= k;
		if (owed < n)
			owed = n;
	}
	return 0;
}

/* Read a reply header [cmd, len_lo, len_hi]; returns the payload length or
 * -1. The top bits of len_hi are masked: they carry the CS flags of 0xC2
 * frames, and no frame is longer than 4 KB. */
static int ch347_reply_header(uint8_t cmd, unsigned int owed)
{
	uint8_t hdr[CH347_HDR_LEN];

	if (ch347_in_take(hdr, sizeof(hdr), owed) < 0)
		return -1;
	if (hdr[0] != cmd) {
		fprintf(stderr, "CH347: bad reply header %02X %02X %02X to 0x%02X\n",
			hdr[0], hdr[1], hdr[2], cmd);
		return -1;
	}
	return hdr[1] | ((hdr[2] & CH347_LEN_HI_MASK) << 8);
}

/* Read a status reply [cmd, len, status, ...] (0xC0, 0xC4: len 1, status 0 =
 * OK). Longer payloads, which some firmware echoes for 0xC0, are skipped by
 * their length. */
static int ch347_read_status(uint8_t cmd)
{
	uint8_t st;
	int len = ch347_reply_header(cmd, 4);

	if (len <= 0) {
		if (len == 0)
			fprintf(stderr, "CH347: empty status reply to 0x%02X\n", cmd);
		return -1;
	}
	if (ch347_in_take(&st, 1, (unsigned int)len) < 0 ||
	    (len > 1 && ch347_in_take(NULL, (unsigned int)len - 1, (unsigned int)len - 1) < 0))
		return -1;
	if (st != 0) {
		fprintf(stderr, "CH347: command 0x%02X failed (status 0x%02X)\n", cmd, st);
		return -1;
	}
	return 0;
}

/* Collect `count` data bytes (dst NULL discards them) from the reply frames
 * [cmd, len, data...] of a 0xC2 or 0xC3 command. */
static int ch347_read_frames(uint8_t cmd, unsigned int count, uint8_t *dst)
{
	unsigned int done = 0;

	while (done < count) {
		int len = ch347_reply_header(cmd, count - done + CH347_HDR_LEN);

		if (len < 0)
			return -1;
		if ((unsigned int)len > count - done) {
			fprintf(stderr, "CH347: reply frame of %d bytes to 0x%02X overruns "
				"(%u of %u bytes)\n", len, cmd, done, count);
			return -1;
		}
		if (ch347_in_take(dst ? dst + done : NULL, (unsigned int)len, count - done) < 0)
			return -1;
		done += (unsigned int)len;
	}
	return 0;
}

#ifndef __EMSCRIPTEN__
/* Discard IN data left over from an interrupted session (e.g. Ctrl-C in the
 * middle of a read), which would otherwise be taken as the config reply.
 * Native only: WebUSB cannot cancel a timed-out transferIn, so a drain read
 * there would swallow the next real reply. */
static void ch347_drain(void)
{
	uint8_t buf[CH347_EP_SIZE];
	int got, ret, total = 0;

	for (int i = 0; i < 1 << 17; i++) {
		got = 0;
		ret = libusb_bulk_transfer(ch347_handle, CH347_EP_IN, buf, sizeof(buf), &got, 20);
		if (ret < 0 || got == 0)
			break;
		total += got;
	}
	if (total)
		DBG("ch347: drained %d stale bytes\n", total);
}
#endif

/* ------------------------------------------------------------------ */
/* SPI primitives */

static int ch347_set_cs(bool assert)
{
	uint8_t cmd[13] = {
		[0] = CH347_CMD_SPI_CS_CTRL,
		[1] = 10,                       /* payload length, LE */
		[3] = assert ? CH347_CS_ASSERT : CH347_CS_DEASSERT,
		/* [4..7] CS0 delays, [8] second CS: left unchanged (0), [9..12] delays */
	};

	DBG("ch347: CS %s\n", assert ? "low" : "high");
	return ch347_bulk_out(cmd, sizeof(cmd));
}

/* In chip-select-flag mode, send a cs_select() that is still deferred:
 * needed before a command that cannot carry the flag (0xC3). */
static int ch347_cs_start_now(void)
{
	if (!ch347_cs_start)
		return 0;
	ch347_cs_start = false;
	return ch347_set_cs(true);
}

/* SPI mode 0, MSB first, software-controlled CS, clock = 60 MHz >> divisor.
 * The 26-byte payload is the vendor StreamHwCfgS struct; the field values are
 * the ones flashrom has shipped since 2023. */
static int ch347_configure(unsigned int divisor)
{
	uint8_t cfg[CH347_HDR_LEN + 26] = {
		[0]  = CH347_CMD_SPI_SET_CFG,
		[1]  = 26,
		[5]  = 0x04, [6] = 0x01,        /* SPI_Mode 0x0104: master */
		[14] = 0x02,                    /* SPI_NSS 0x0200: software CS */
		[15] = (divisor & 0x7) << 3,    /* SPI_BaudRatePrescaler */
		[19] = 0x07,                    /* SPI_CRCPolynomial */
	};

	if (ch347_bulk_out(cfg, sizeof(cfg)) < 0 || ch347_read_status(CH347_CMD_SPI_SET_CFG) < 0) {
		fprintf(stderr, "CH347: SPI configuration failed\n");
		return -1;
	}
	ch347_in_reset();   /* like flashrom, drop anything else in that reply */
	return 0;
}

/* Firmware version from the chip-info read [CA 01 00 00], as WCH's Linux
 * driver reads it (reply bytes 3..4, little endian). -1 if unreadable. */
static int ch347_firmware_version(void)
{
	static const uint8_t cmd[] = { CH347_CMD_INFO_RD, 1, 0, 0 };
	uint8_t info[2];

	if (ch347_bulk_out(cmd, sizeof(cmd)) < 0)
		return -1;
	int len = ch347_reply_header(CH347_CMD_INFO_RD, 4);
	if (len < 2 || ch347_in_take(info, 2, (unsigned int)len) < 0 ||
	    ch347_in_take(NULL, (unsigned int)len - 2, (unsigned int)len - 2) < 0) {
		ch347_in_reset();       /* do not leave a partial reply buffered */
		return -1;
	}
	return info[0] | (info[1] << 8);
}

/* One write frame of up to 507 bytes. In chip-select-flag mode it is a 0xC2
 * frame (whose reply, the bytes clocked in, is discarded) carrying a deferred
 * cs_select() and, if `release`, the CS release; otherwise a 0xC4 frame. */
static int ch347_write_frame(const uint8_t *data, unsigned int n, bool release)
{
	uint8_t frame[CH347_OUT_FRAME];

	memcpy(frame + CH347_HDR_LEN, data, n);
	frame[1] = n & 0xFF;
	if (!ch347_csflags) {
		frame[0] = CH347_CMD_SPI_OUT;
		frame[2] = (n >> 8) & 0xFF;
		return ch347_bulk_out(frame, n + CH347_HDR_LEN) < 0 ||
		       ch347_read_status(CH347_CMD_SPI_OUT) < 0 ? -1 : 0;
	}
	frame[0] = CH347_CMD_SPI_OUT_IN;
	frame[2] = ((n >> 8) & CH347_LEN_HI_MASK) |
		   (ch347_cs_start ? CH347_C2_CS0_START : 0) |
		   (release ? CH347_C2_CS0_STOP : 0);
	ch347_cs_start = false;
	return ch347_bulk_out(frame, n + CH347_HDR_LEN) < 0 ||
	       ch347_read_frames(CH347_CMD_SPI_OUT_IN, n, NULL) < 0 ? -1 : 0;
}

/* Send the held writes; `release` asks the last frame to release CS (only
 * honored in chip-select-flag mode). */
static int ch347_flush(bool release)
{
	const uint8_t *p = ch347_pend;
	unsigned int n = ch347_pend_len;

	ch347_pend_len = 0;
	while (n) {
		unsigned int k = n < CH347_OUT_PAYLOAD ? n : CH347_OUT_PAYLOAD;

		if (ch347_write_frame(p, k, release && k == n) < 0)
			return -1;
		p += k;
		n -= k;
	}
	return 0;
}

/* Hold writes back, sending a frame each time the buffer fills. */
static int ch347_queue(unsigned int writecnt, const uint8_t *writearr)
{
	while (writecnt) {
		unsigned int room = CH347_OUT_PAYLOAD - ch347_pend_len;
		unsigned int n = writecnt < room ? writecnt : room;

		memcpy(ch347_pend + ch347_pend_len, writearr, n);
		ch347_pend_len += n;
		writearr += n;
		writecnt -= n;
		if (ch347_pend_len == CH347_OUT_PAYLOAD && ch347_flush(false) < 0)
			return -1;
	}
	return 0;
}

/* Stream `readcnt` bytes in with 0xC3 (MOSI idles at the configured default). */
static int ch347_read(unsigned int readcnt, uint8_t *readarr)
{
	uint8_t cmd[7] = {
		[0] = CH347_CMD_SPI_IN,
		[1] = 4,
		[3] = readcnt & 0xFF,
		[4] = (readcnt >> 8) & 0xFF,
		[5] = (readcnt >> 16) & 0xFF,
		[6] = (readcnt >> 24) & 0xFF,
	};

	if (ch347_cs_start_now() < 0 || ch347_bulk_out(cmd, sizeof(cmd)) < 0)
		return -1;
	return ch347_read_frames(CH347_CMD_SPI_IN, readcnt, readarr);
}

/* Shift out the held writes and `readcnt` 0xFF filler bytes as one 0xC2
 * frame, keeping the bytes clocked in during the filler. The caller ensures
 * it all fits one frame, the size every firmware answers in one frame. A
 * deferred cs_select() rides along as a flag; CS stays low afterwards. */
static int ch347_write_read(unsigned int readcnt, uint8_t *readarr)
{
	uint8_t frame[CH347_OUT_FRAME];
	uint8_t in[CH347_OUT_PAYLOAD];
	unsigned int w = ch347_pend_len;
	unsigned int n = w + readcnt;

	frame[0] = CH347_CMD_SPI_OUT_IN;
	frame[1] = n & 0xFF;
	frame[2] = ((n >> 8) & CH347_LEN_HI_MASK) | (ch347_cs_start ? CH347_C2_CS0_START : 0);
	memcpy(frame + CH347_HDR_LEN, ch347_pend, w);
	memset(frame + CH347_HDR_LEN + w, 0xFF, readcnt);
	ch347_pend_len = 0;
	ch347_cs_start = false;

	if (ch347_bulk_out(frame, n + CH347_HDR_LEN) < 0 ||
	    ch347_read_frames(CH347_CMD_SPI_OUT_IN, n, in) < 0)
		return -1;
	memcpy(readarr, in + w, readcnt);
	return 0;
}

/* ------------------------------------------------------------------ */
/* Programmer interface */

/* CS is driven separately through cs_select()/cs_deselect(), as with the
 * other drivers, so one flash command may span several calls. Writes are
 * held back (see the top of this file), so a write's USB errors may only
 * surface at the next read or at cs_deselect. */
int ch347_spi_send_command(unsigned int writecnt, unsigned int readcnt,
			   const unsigned char *writearr, unsigned char *readarr)
{
	if (ch347_handle == NULL || ch347_failed)
		return -1;

	trace_dump("SPI WRITE", writearr, writecnt);
	if (writecnt && ch347_queue(writecnt, writearr) < 0)
		goto fail;
	if (readcnt) {
		int ret;

		/* Pure reads keep using 0xC3, the path flashrom has shipped
		 * since 2023; 0xC2 only folds held writes into a short read. */
		if (ch347_pend_len && ch347_pend_len + readcnt <= CH347_OUT_PAYLOAD)
			ret = ch347_write_read(readcnt, readarr);
		else
			ret = ch347_flush(false) < 0 ? -1 : ch347_read(readcnt, readarr);
		if (ret < 0)
			goto fail;
		trace_dump("SPI READ", readarr, readcnt);
	}
	return 0;

fail:
	ch347_failed = true;
	return -1;
}

static int ch347_cs_select(void)
{
	if (ch347_handle == NULL || ch347_failed)
		return -1;
	if (ch347_flush(false) < 0)
		goto fail;
	if (ch347_csflags) {
		ch347_cs_start = true;          /* sent with the first frame */
		return 0;
	}
	if (ch347_set_cs(true) < 0)
		goto fail;
	return 0;

fail:
	ch347_failed = true;
	return -1;
}

static int ch347_cs_deselect(void)
{
	if (ch347_handle == NULL || ch347_failed)
		return -1;

	int ret = 0;
	if (ch347_csflags && ch347_pend_len) {
		ret = ch347_flush(true);        /* last frame releases CS */
	} else if (ch347_csflags && ch347_cs_start) {
		ch347_cs_start = false;         /* nothing was sent: CS never went low */
	} else {
		ret = ch347_flush(false);
		if (ch347_set_cs(false) < 0)    /* raise CS even if the flush failed */
			ret = -1;
	}
	if (ret < 0)
		ch347_failed = true;
	return ret;
}

/* Pick the fastest clock that does not exceed the requested rate. */
static unsigned int ch347_divisor_for(unsigned int khz)
{
	unsigned int div = 0;

	if (khz == 0)
		return CH347_DEFAULT_DIV;
	if (khz > 60000)
		fprintf(stderr, "CH347: %u kHz is above the fastest clock, using 60 MHz\n", khz);
	while (div < 7 && (60000u >> div) > khz)
		div++;
	if ((60000u >> div) > khz)
		fprintf(stderr, "CH347: %u kHz is below the slowest clock, using 468.75 kHz\n", khz);
	return div;
}

static unsigned int ch347_divisor_for_speed(void)
{
	return ch347_divisor_for(spi_controller_get_speed_khz());
}

/* Reset the command and reply state, configure the clock, raise CS, and
 * choose chip-select-flag mode from the firmware version. */
static int ch347_setup(void)
{
	ch347_pend_len = 0;
	ch347_csflags = false;
	ch347_cs_start = false;
	ch347_failed = false;
	ch347_in_reset();

	ch347_divisor = ch347_divisor_for_speed();
	if (ch347_configure(ch347_divisor) < 0 || ch347_set_cs(false) < 0)
		goto fail;
	printf("CH347 SPI clock: %.4g MHz\n", 60.0 / (1u << ch347_divisor));

	int fw = ch347_firmware_version();
	bool capable = ch347_is_f || (fw >= 0 && fw >= CH347T_CS_FLAGS_MIN_FW);
	if (fw < 0)
		printf("CH347 firmware version: unknown\n");
	else
		printf("CH347 firmware version: %04x\n", fw);
	if (spi_controller_get_options() & SPI_OPT_NO_CS_FLAGS)
		printf("CH347 chip-select flags: off (disabled)\n");
	else if (!capable)
		printf("CH347 chip-select flags: off (needs CH347T firmware %04x or newer)\n",
		       CH347T_CS_FLAGS_MIN_FW);
	else {
		ch347_csflags = true;
		printf("CH347 chip-select flags: on\n");
	}
	return 0;

fail:
	ch347_failed = true;
	return -1;
}

static void ch347_release(void)
{
	libusb_release_interface(ch347_handle, ch347_iface);
	libusb_close(ch347_handle);
	libusb_exit(NULL);
	ch347_handle = NULL;
	ch347_iface = -1;
}

int ch347_spi_init(void)
{
	const char *model = NULL;
	int ret;

	if (ch347_handle != NULL) {
		fprintf(stderr, "%s: handle already set!\n", __func__);
		return -1;
	}

	if (libusb_init(NULL) < 0) {
		fprintf(stderr, "CH347: could not initialize libusb\n");
		return -1;
	}

	for (size_t i = 0; i < sizeof(ch347_models) / sizeof(ch347_models[0]); i++) {
		ch347_handle = libusb_open_device_with_vid_pid(NULL, CH347_VID, ch347_models[i].pid);
		if (ch347_handle) {
			ch347_iface = ch347_models[i].iface;
			ch347_is_f = ch347_models[i].is_f;
			model = ch347_models[i].name;
			break;
		}
	}
	if (ch347_handle == NULL) {
		DBG("ch347: no %04x:%04x or %04x:%04x device\n",
		    CH347_VID, CH347T_PID, CH347_VID, CH347F_PID);
		libusb_exit(NULL);
		return -1;
	}

	printf("Found programmer device: WinChipHead (WCH) - %s\n", model);

	/* Only the SPI interface is claimed; the UART interfaces keep their
	 * serial driver. Auto-detach also handles WCH's ch34x_mphsi module. */
	ret = libusb_set_auto_detach_kernel_driver(ch347_handle, 1);
	if (ret != 0 && ret != LIBUSB_ERROR_NOT_SUPPORTED)
		DBG("ch347: auto-detach unavailable: %s\n", libusb_error_name(ret));

	ret = libusb_claim_interface(ch347_handle, ch347_iface);
	if (ret != 0) {
		fprintf(stderr, "CH347: failed to claim interface %d: %s\n",
			ch347_iface, libusb_error_name(ret));
		libusb_close(ch347_handle);
		libusb_exit(NULL);
		ch347_handle = NULL;
		ch347_iface = -1;
		return -1;
	}

	struct libusb_device *dev = libusb_get_device(ch347_handle);
	struct libusb_device_descriptor desc;
	if (dev && libusb_get_device_descriptor(dev, &desc) == 0)
		printf("Device revision is %d.%01d.%01d\n",
		       (desc.bcdDevice >> 8) & 0x00FF,
		       (desc.bcdDevice >> 4) & 0x000F,
		       (desc.bcdDevice >> 0) & 0x000F);

#ifndef __EMSCRIPTEN__
	ch347_drain();
#endif

	if (ch347_setup() < 0) {
		ch347_release();
		return -1;
	}
	return 0;
}

int ch347_spi_shutdown(void)
{
	if (ch347_handle == NULL)
		return 0;

	if (!ch347_failed) {
		ch347_flush(false);
		ch347_set_cs(false);
	}
	ch347_pend_len = 0;
	ch347_cs_start = false;
	ch347_release();
	DBG("ch347: shutdown complete\n");
	return 0;
}

/* Recover after a WebUSB stall or reconnect: clear both endpoints, drop any
 * held writes, and set up again. Also picks up a changed SPI clock or
 * option. */
static int ch347_spi_reinit(void)
{
	if (ch347_handle == NULL)
		return -1;
	libusb_clear_halt(ch347_handle, CH347_EP_IN);
	libusb_clear_halt(ch347_handle, CH347_EP_OUT);
	return ch347_setup();
}

const struct spi_programmer ch347_programmer = {
	.name           = "CH347",
	.init           = ch347_spi_init,
	.shutdown       = ch347_spi_shutdown,
	.send_command   = ch347_spi_send_command,
	.cs_select      = ch347_cs_select,
	.cs_deselect    = ch347_cs_deselect,
	.libusb_version = get_libusb_version,
	.reinit         = ch347_spi_reinit,
};

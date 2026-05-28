#include "ch341a_spi.h"
#include "spi_controller.h"
#include "usb_hal.h"
#include <libusb-1.0/libusb.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

extern int debug_enabled;
extern int trace_enabled;

struct libusb_device_handle *handle = NULL;
static struct usb_hal *hal = NULL;

static void trace_dump(const char *label, const unsigned char *buf,
                       unsigned int len) {
  if (!trace_enabled || !len)
    return;
  fprintf(stderr, "[TRACE] %s (%u byte%s):", label, len, (len == 1) ? "" : "s");
  for (unsigned int i = 0; i < len; i++)
    fprintf(stderr, " %02x", buf[i]);
  fprintf(stderr, "\n");
}

#define USB_TIMEOUT 1000
#define WRITE_EP 0x02
#define READ_EP 0x82

#define CH341_PACKET_LENGTH 0x20
#define CH341_MAX_PACKETS 256
#define CH341_MAX_PACKET_LEN (CH341_PACKET_LENGTH * CH341_MAX_PACKETS)

#define CH341A_CMD_SET_OUTPUT 0xA1
#define CH341A_CMD_IO_ADDR 0xA2
#define CH341A_CMD_PRINT_OUT 0xA3
#define CH341A_CMD_SPI_STREAM 0xA8
#define CH341A_CMD_SIO_STREAM 0xA9
#define CH341A_CMD_I2C_STREAM 0xAA
#define CH341A_CMD_UIO_STREAM 0xAB

#define CH341A_CMD_I2C_STM_START 0x74
#define CH341A_CMD_I2C_STM_STOP 0x75
#define CH341A_CMD_I2C_STM_OUT 0x80
#define CH341A_CMD_I2C_STM_IN 0xC0
#define CH341A_CMD_I2C_STM_MAX (min(0x3F, CH341_PACKET_LENGTH))
#define CH341A_CMD_I2C_STM_SET 0x60
#define CH341A_CMD_I2C_STM_US 0x40
#define CH341A_CMD_I2C_STM_MS 0x50
#define CH341A_CMD_I2C_STM_DLY 0x0F
#define CH341A_CMD_I2C_STM_END 0x00

#define CH341A_CMD_UIO_STM_IN 0x00
#define CH341A_CMD_UIO_STM_DIR 0x40
#define CH341A_CMD_UIO_STM_OUT 0x80
#define CH341A_CMD_UIO_STM_US 0xC0
#define CH341A_CMD_UIO_STM_END 0x20

#define CH341A_STM_I2C_20K 0x00
#define CH341A_STM_I2C_100K 0x01
#define CH341A_STM_I2C_400K 0x02
#define CH341A_STM_I2C_750K 0x03
#define CH341A_STM_SPI_DBL 0x04

struct dev_entry {
  uint16_t vendor_id;
  uint16_t device_id;
  const char *vendor_name;
  const char *device_name;
};

#ifdef __EMSCRIPTEN__
static uint8_t em_safe_wbuf[CH341_MAX_PACKETS + 1][CH341_PACKET_LENGTH];
static uint8_t em_safe_rbuf[CH341_MAX_PACKET_LEN];
#endif

const struct dev_entry devs_ch341a_spi[] = {
    {0x1A86, 0x5512, "WinChipHead (WCH)", "CH341A"},
    {0},
};

int config_stream(unsigned int speed) {
  if (!hal) {
    if (debug_enabled)
      fprintf(stderr, "[DEBUG] config_stream: HAL is NULL\n");
    return -1;
  }
  if (debug_enabled)
    fprintf(stderr,
            "[DEBUG] config_stream: configuring stream with speed=0x%x\n",
            speed);

  uint8_t buf[] = {CH341A_CMD_I2C_STREAM,
                   CH341A_CMD_I2C_STM_SET | (speed & 0x7),
                   CH341A_CMD_I2C_STM_END};

  int32_t ret = usb_hal_ch341_transfer(hal, sizeof(buf), 0, buf, NULL);
  if (ret < 0) {
    if (debug_enabled)
      fprintf(stderr, "[DEBUG] config_stream: stream configuration failed\n");
  } else if (debug_enabled) {
    fprintf(stderr, "[DEBUG] config_stream: stream configured successfully\n");
  }
  return ret;
}

static uint8_t swap_byte(uint8_t x) {
  x = ((x >> 1) & 0x55) | ((x << 1) & 0xaa);
  x = ((x >> 2) & 0x33) | ((x << 2) & 0xcc);
  x = ((x >> 4) & 0x0f) | ((x << 4) & 0xf0);
  return x;
}

/* Embed CS assertion in a USB packet buffer for single-transfer SPI.
 * Follows the OUT-repetition pattern required by CH341A rev 0x30:
 * 4x CS-high, 1x CS-low, then DIR strobe, matching native enable_pins().
 * All inside the same USB bulk transfer that carries the SPI stream.
 */
static uint8_t *pluck_cs(uint8_t *ptr) {
  *ptr++ = CH341A_CMD_UIO_STREAM;
  *ptr++ = CH341A_CMD_UIO_STM_OUT | CH341A_UIO_STATE_CS_HIGH_SCK_LOW;
  *ptr++ = CH341A_CMD_UIO_STM_OUT | CH341A_UIO_STATE_CS_HIGH_SCK_LOW;
  *ptr++ = CH341A_CMD_UIO_STM_OUT | CH341A_UIO_STATE_CS_HIGH_SCK_LOW;
  *ptr++ = CH341A_CMD_UIO_STM_OUT | CH341A_UIO_STATE_CS_HIGH_SCK_LOW;
  *ptr++ = CH341A_CMD_UIO_STM_OUT | CH341A_UIO_STATE_CS0_LOW_SCK_LOW;
  *ptr++ = CH341A_CMD_UIO_STM_DIR | CH341A_UIO_DIR_ALL_OUTPUT;
  *ptr++ = CH341A_CMD_UIO_STM_END;
  return ptr;
}

/* Deassert CS after a SPI transaction.
 *
 * Native (libusb):  send as many OUT repetitions as enable_pins uses
 *   so the output state reliably latches on CH341A rev 0x30.
 * WASM (WebUSB):    one OUT + DIR + END — each byte is individually
 *   submitted, so repeating OUT would toggle the pin. */
static int cs_deassert(void) {
#ifdef __EMSCRIPTEN__
  uint8_t buf[] = {
      CH341A_CMD_UIO_STREAM,
      CH341A_CMD_UIO_STM_OUT | CH341A_UIO_STATE_CS_HIGH_SCK_LOW,
      CH341A_CMD_UIO_STM_DIR | CH341A_UIO_DIR_ALL_OUTPUT,
      CH341A_CMD_UIO_STM_END,
  };
#else
  uint8_t buf[] = {
      CH341A_CMD_UIO_STREAM,
      CH341A_CMD_UIO_STM_OUT | CH341A_UIO_STATE_CS_HIGH_SCK_LOW,
      CH341A_CMD_UIO_STM_OUT | CH341A_UIO_STATE_CS_HIGH_SCK_LOW,
      CH341A_CMD_UIO_STM_OUT | CH341A_UIO_STATE_CS_HIGH_SCK_LOW,
      CH341A_CMD_UIO_STM_OUT | CH341A_UIO_STATE_CS_HIGH_SCK_LOW,
      CH341A_CMD_UIO_STM_DIR | CH341A_UIO_DIR_ALL_OUTPUT,
      CH341A_CMD_UIO_STM_END,
  };
#endif
  return usb_hal_ch341_transfer(hal, sizeof(buf), 0, buf, NULL);
}

/* Enable or disable the CH341A's SPI output pins — used only at
 * init / shutdown, NOT per-transaction (pluck_cs / cs_deassert handle
 * the per-transaction CS lifecycle).
 *
 * The CH341A UIO stream requires repeated GPIO state commands before the
 * DIR command takes effect. The difference between native and WASM paths:
 *
 * Native (libusb):  five OUT commands followed by DIR and END.
 *   The first four set CS/SCK to idle-high, the fifth drops CS low.
 *   This sequence was empirically determined for CH341A rev 0x30.
 *
 * WASM (WebUSB):    one OUT command + DIR + END.
 *   Each USB packet is individually submitted to the browser's WebUSB,
 *   and the CH341A processes them one at a time. Multiple identical
 *   consecutive OUT commands are redundant.
 *
 * Without this ifdef, the WASM path would toggle CS on each of the five
 * OUT commands, glitching the flash chip's CS line. */
int enable_pins(bool enable) {
  if (debug_enabled)
    fprintf(stderr, "[DEBUG] enable_pins: %sabling output pins\n",
            enable ? "en" : "dis");

#ifdef __EMSCRIPTEN__
  uint8_t buf[] = {
      CH341A_CMD_UIO_STREAM,
      CH341A_CMD_UIO_STM_OUT | (enable ? CH341A_UIO_STATE_CS0_LOW_SCK_LOW
                                       : CH341A_UIO_STATE_CS_HIGH_SCK_LOW),
      CH341A_CMD_UIO_STM_DIR |
          (enable ? CH341A_UIO_DIR_ALL_OUTPUT : CH341A_UIO_DIR_INPUT),
      CH341A_CMD_UIO_STM_END,
  };
#else
  uint8_t buf[] = {
      CH341A_CMD_UIO_STREAM,
      CH341A_CMD_UIO_STM_OUT | CH341A_UIO_STATE_CS_HIGH_SCK_LOW,
      CH341A_CMD_UIO_STM_OUT | CH341A_UIO_STATE_CS_HIGH_SCK_LOW,
      CH341A_CMD_UIO_STM_OUT | CH341A_UIO_STATE_CS_HIGH_SCK_LOW,
      CH341A_CMD_UIO_STM_OUT | CH341A_UIO_STATE_CS_HIGH_SCK_LOW,
      CH341A_CMD_UIO_STM_OUT | CH341A_UIO_STATE_CS0_LOW_SCK_LOW,
      CH341A_CMD_UIO_STM_DIR |
          (enable ? CH341A_UIO_DIR_ALL_OUTPUT : CH341A_UIO_DIR_INPUT),
      CH341A_CMD_UIO_STM_END,
  };
#endif

  int32_t ret = usb_hal_ch341_transfer(hal, sizeof(buf), 0, buf, NULL);
  if (ret < 0) {
    fprintf(stderr, "Could not %sable output pins.\n", enable ? "en" : "dis");
    if (debug_enabled)
      fprintf(stderr, "[DEBUG] enable_pins: failed to %sable pins\n",
              enable ? "en" : "dis");
  } else if (debug_enabled) {
    fprintf(stderr, "[DEBUG] enable_pins: pins %sabled successfully\n",
            enable ? "en" : "dis");
  }
  return ret;
}

int ch341a_spi_send_command(unsigned int writecnt, unsigned int readcnt,
                            const unsigned char *writearr,
                            unsigned char *readarr) {
  int32_t ret = 0;

  if (debug_enabled)
    fprintf(stderr,
            "[DEBUG] ch341a_spi_send_command: sending command (write=%u, "
            "read=%u)\n",
            writecnt, readcnt);

  trace_dump("SPI WRITE", writearr, writecnt);

  if (!hal) {
    if (debug_enabled)
      fprintf(stderr, "[DEBUG] ch341a_spi_send_command: HAL is NULL\n");
    return -1;
  }

  const size_t packets = (writecnt + readcnt + CH341_PACKET_LENGTH - 2) /
                         (CH341_PACKET_LENGTH - 1);

#ifdef __EMSCRIPTEN__
  if (packets + 1 > CH341_MAX_PACKETS + 1 ||
      writecnt + readcnt > CH341_MAX_PACKET_LEN) {
    fprintf(stderr,
            "ch341a_spi_send_command: transfer too large for static buffers\n");
    return -1;
  }
  uint8_t (*wbuf)[CH341_PACKET_LENGTH] = em_safe_wbuf;
  uint8_t *rbuf = em_safe_rbuf;
#else
  uint8_t wbuf[packets + 1][CH341_PACKET_LENGTH];
  uint8_t rbuf[writecnt + readcnt];
#endif
  memset(wbuf[0], 0, CH341_PACKET_LENGTH);
  pluck_cs(wbuf[0]);

  unsigned int write_left = writecnt;
  unsigned int read_left = readcnt;
  unsigned int p;
  for (p = 0; p < packets; p++) {
    unsigned int write_now = min(CH341_PACKET_LENGTH - 1, write_left);
    unsigned int read_now =
        min((CH341_PACKET_LENGTH - 1) - write_now, read_left);
    uint8_t *ptr = wbuf[p + 1];
    *ptr++ = CH341A_CMD_SPI_STREAM;
    unsigned int i;
    for (i = 0; i < write_now; ++i)
      *ptr++ = swap_byte(*writearr++);
    if (read_now) {
      memset(ptr, 0xFF, read_now);
      read_left -= read_now;
    }
    write_left -= write_now;
  }

  ret = usb_hal_ch341_transfer(
      hal, CH341_PACKET_LENGTH + packets + writecnt + readcnt,
      writecnt + readcnt, wbuf[0], rbuf);

  cs_deassert();

  if (ret < 0)
    return -1;

  unsigned int i;
  for (i = 0; i < readcnt; i++)
    readarr[i] = swap_byte(rbuf[writecnt + i]);

  trace_dump("SPI READ", readarr, readcnt);
  return 0;
}

int ch341a_spi_shutdown(void) {
  if (debug_enabled)
    fprintf(stderr, "[DEBUG] ch341a_spi_shutdown: shutting down CH341A\n");

  if (!hal)
    return -1;

  usb_hal_suppress_errors(hal, 1);
  enable_pins(false);
  usb_hal_suppress_errors(hal, 0);

  usb_hal_release_interface(hal, 0);
  usb_hal_close(hal);
  usb_hal_free(hal);
  hal = NULL;
  handle = NULL;

  if (debug_enabled)
    fprintf(stderr, "[DEBUG] ch341a_spi_shutdown: shutdown complete\n");
  return 0;
}

const char *get_libusb_version(void) {
  return usb_hal_backend_version();
}

int ch341a_spi_init(void) {
  if (debug_enabled)
    fprintf(stderr, "[DEBUG] ch341a_spi_init: initializing CH341A\n");

  if (hal) {
    fprintf(stderr, "%s: HAL already initialized!\n", __func__);
    return -1;
  }

  hal = usb_hal_create();
  if (!hal) {
    fprintf(stderr, "Couldn't allocate USB HAL!\n");
    return -1;
  }

  if (debug_enabled)
    fprintf(stderr, "[DEBUG] ch341a_spi_init: calling usb_hal_init\n");

  if (usb_hal_init(hal) < 0) {
    fprintf(stderr, "Couldn't initialize USB HAL!\n");
    usb_hal_free(hal);
    hal = NULL;
    return -1;
  }

  if (debug_enabled)
    fprintf(stderr, "[DEBUG] ch341a_spi_init: USB HAL initialized\n");

  if (debug_enabled)
    usb_hal_set_debug(hal, 1);

  uint16_t vid = devs_ch341a_spi[0].vendor_id;
  uint16_t pid = devs_ch341a_spi[0].device_id;

  if (debug_enabled)
    fprintf(stderr, "[DEBUG] ch341a_spi_init: opening device %04x:%04x\n", vid,
            pid);

  if (usb_hal_open_vid_pid(hal, vid, pid) < 0) {
    if (programmer_type != PROGRAMMER_AUTO)
      fprintf(stderr, "Couldn't open device %04x:%04x.\n", vid, pid);
    goto fail;
  }
  handle = (struct libusb_device_handle *)usb_hal_get_handle(hal);

  if (debug_enabled)
    fprintf(stderr, "[DEBUG] ch341a_spi_init: device opened successfully\n");

#ifndef __EMSCRIPTEN__
#if defined(__gnu_linux__)
  if (debug_enabled)
    fprintf(stderr,
            "[DEBUG] ch341a_spi_init: attempting to detach kernel driver\n");

  usb_hal_detach_kernel_driver(hal, 0);

  if (debug_enabled)
    fprintf(stderr,
            "[DEBUG] ch341a_spi_init: kernel driver detached (or not found)\n");
#endif
#endif

  if (debug_enabled)
    fprintf(stderr, "[DEBUG] ch341a_spi_init: claiming interface 0\n");

  if (usb_hal_claim_interface(hal, 0) != 0) {
    fprintf(stderr, "Failed to claim interface 0.\n");
    goto close;
  }

  if (debug_enabled)
    fprintf(stderr, "[DEBUG] ch341a_spi_init: interface 0 claimed\n");

  uint16_t bcd = 0;
  if (usb_hal_get_bcd_device(hal, &bcd) == 0) {
    printf("Device CH341A rev.%d.%01d.%01d\n", (bcd >> 8) & 0xFF,
           (bcd >> 4) & 0x0F, bcd & 0x0F);
  } else {
    fprintf(stderr, "Failed to get device descriptor.\n");
    goto release;
  }

  if (debug_enabled)
    fprintf(stderr,
            "[DEBUG] ch341a_spi_init: configuring stream and enabling pins\n");

  usb_hal_suppress_errors(hal, 1);
  int cfg_ret = config_stream(CH341A_STM_I2C_20K);
  usb_hal_suppress_errors(hal, 0);

  if (cfg_ret < 0 || enable_pins(true) < 0) {
    if (debug_enabled)
      fprintf(stderr,
              "[DEBUG] ch341a_spi_init: first stream config failed, trying USB "
              "reset\n");
    usb_hal_reset_device(hal);
    if (debug_enabled)
      fprintf(stderr,
              "[DEBUG] ch341a_spi_init: USB reset done, retrying stream "
              "config\n");
    if (config_stream(CH341A_STM_I2C_20K) < 0 || enable_pins(true) < 0) {
      fprintf(stderr, "Could not configure stream interface.\n");
      if (debug_enabled)
        fprintf(stderr, "[DEBUG] ch341a_spi_init: retry still failed\n");
      goto release;
    }
  }

  if (debug_enabled)
    fprintf(stderr, "[DEBUG] ch341a_spi_init: initialization complete\n");
  return 0;

release:
  usb_hal_release_interface(hal, 0);
close:
  usb_hal_close(hal);
fail:
  usb_hal_exit(hal);
  usb_hal_free(hal);
  hal = NULL;
  handle = NULL;
  return -1;
}

#ifdef __EMSCRIPTEN__
int ch341a_spi_reinit(void) {
  if (!hal)
    return -1;
  usb_hal_clear_halt(hal, READ_EP);
  usb_hal_clear_halt(hal, WRITE_EP);
  if (config_stream(CH341A_STM_I2C_750K) < 0)
    return -1;
  if (enable_pins(true) < 0)
    return -1;
  return 0;
}
#endif

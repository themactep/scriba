ifeq ($(CONFIG_STATIC), yes)
TARGET     = scriba-static
else
TARGET     = scriba
endif

PKG        = $(TARGET)
GIT_COMMIT_DATE = $(shell git log -1 --format="%cd" --date=format:%Y%m%d)
GIT_COMMIT_HASH = $(shell git rev-parse --short HEAD)
VERSION    = $(GIT_COMMIT_DATE)-$(GIT_COMMIT_HASH)

DL_DIR    ?= $(CURDIR)/dl
BUILD_DIR  = $(CURDIR)/build
TARGET_DIR = $(BUILD_DIR)/bin
TARGET_BIN = $(TARGET_DIR)/$(TARGET)
LOCAL_SRC_DIR = $(BUILD_DIR)/src
LOCAL_INSTALL_PREFIX = $(BUILD_DIR)/usr
SRC_DIR    = src

CC        ?= gcc
STRIP     ?= strip
INSTALL   ?= install
PREFIX    ?= /usr
BINDIR    ?= $(PREFIX)/bin
CFLAGS     = -std=gnu99 -Wall -O2 -D_FILE_OFFSET_BITS=64 -DGIT_COMMIT_DATE=\"$(GIT_COMMIT_DATE)\" -DGIT_COMMIT_HASH=\"$(GIT_COMMIT_HASH)\"
LDFLAGS   ?= -pthread
LIBS      ?= -lusb-1.0

EEPROM_SUPPORT = yes

SRCS  = src/flashcmd_api.c \
	src/spi_controller.c \
	src/spi_nand_flash.c \
	src/spi_nand_flash_protocol.c \
	src/spi_nand_flash_tables.c \
	src/spi_nor_flash.c \
	src/ch341a_spi.c \
	src/timer.c \
	src/main.c

LIBUSB_VERSION := 1.0.28
LIBUSB_BUNDLE := libusb-$(LIBUSB_VERSION).tar.bz2
LIBUSB_TARBALL_URL := https://github.com/libusb/libusb/releases/download/v$(LIBUSB_VERSION)/$(LIBUSB_BUNDLE)
LIBUSB_TARBALL := $(DL_DIR)/$(LIBUSB_BUNDLE)
LIBUSB_SRC_DIR := $(LOCAL_SRC_DIR)/libusb-$(LIBUSB_VERSION)
STATIC_LIBUSB = $(LOCAL_INSTALL_PREFIX)/lib/libusb-1.0.a

ifeq ($(CONFIG_STATIC), yes)
# Adjust flags for locally built static libusb
CFLAGS  += -DCONFIG_STATIC
CFLAGS  += -I$(LOCAL_INSTALL_PREFIX)/include/libusb-1.0
LDFLAGS += -L$(LOCAL_INSTALL_PREFIX)/lib -static
LIBS    += -pthread
else
LIBS    += -ludev
endif

ifeq ($(EEPROM_SUPPORT), yes)
CFLAGS += -DEEPROM_SUPPORT
SRCS += src/ch341a_i2c.c \
	src/i2c_eeprom.c \
	src/spi_eeprom.c \
	src/bitbang_microwire.c \
	src/mw_eeprom.c \
	src/ch341a_gpio.c
endif

.PHONY: all clean strip install

all: $(TARGET) $(TARGET_BIN) strip
	@echo "Build complete. Run 'make install' to install."

scriba:
	@echo "Building $(TARGET) with dynamic libusb..."

scriba-static: $(STATIC_LIBUSB)
	@echo "Building $(TARGET) with static libusb..."

static:
	CONFIG_STATIC=yes $(MAKE)

$(TARGET_BIN): $(SRCS)
	@echo "Building $(TARGET)..."
	mkdir -p $(BUILD_DIR)
	mkdir -p $(TARGET_DIR)
	$(CC) $(CFLAGS) $(SRCS) $(LDFLAGS) $(LIBS) -o $@

$(STATIC_LIBUSB): $(LIBUSB_SRC_DIR)/configure
	@echo "Building static libusb..."
	@cd $(LIBUSB_SRC_DIR); \
	./configure --prefix="$(LOCAL_INSTALL_PREFIX)" --enable-static --disable-shared --disable-udev || exit 1; \
	$(MAKE); \
	$(MAKE) install

$(LIBUSB_SRC_DIR)/configure: $(LIBUSB_TARBALL)
	[ -d $(LOCAL_SRC_DIR) ] || mkdir -p $(LOCAL_SRC_DIR)
	[ -f $@ ] || tar xjf $(LIBUSB_TARBALL) -C $(LOCAL_SRC_DIR)

$(LIBUSB_TARBALL):
	@echo "Checking for libusb tarball..."
	[ -d $(DL_DIR) ] || mkdir -p $(DL_DIR)
	[ -f $(LIBUSB_TARBALL) ] || curl -sL -o $(LIBUSB_TARBALL) "$(LIBUSB_TARBALL_URL)"
	[ -f $(LIBUSB_TARBALL) ] || { echo "Failed to download libusb tarball"; exit 1; }

clean:
	rm -rf $(BUILD_DIR)

strip: $(TARGET_BIN)
	$(STRIP) $(TARGET_BIN)

install: $(TARGET_BIN)
	$(INSTALL) -m 0755 -D $(TARGET_BIN) $(DESTDIR)$(BINDIR)/scriba
	$(INSTALL) -m 0664 -D $(SRC_DIR)/40-persistent-ch341a.rules $(DESTDIR)/etc/udev/rules.d/40-persistent-ch341a.rules

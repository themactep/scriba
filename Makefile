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
CFLAGS     = -std=gnu99 -Wall -O2 -D_FILE_OFFSET_BITS=64 -DGIT_COMMIT_DATE=\"$(GIT_COMMIT_DATE)\" -DGIT_COMMIT_HASH=\"$(GIT_COMMIT_HASH)\" -I$(SRC_DIR)
LDFLAGS   ?= -pthread
LIBS      ?= -lusb-1.0

EEPROM_SUPPORT = yes

SRCS  = src/flashcmd_api.c \
	src/spi_controller.c \
	src/spi_nand_flash.c \
	src/spi_nand_flash_protocol.c \
	src/spi_nand_flash_tables.c \
	src/spi_nor_flash.c \
	src/spi_nor_flash_tables.c \
	src/ch341a_spi.c \
	src/ezp2019_spi.c \
	src/usb_hal_libusb.c \
	src/scriba.c \
	src/timer.c \
	frontends/cli/main.c

LIBUSB_VERSION := 1.0.30
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

.PHONY: all clean strip install web web-dist web-serve check-emsdk help

all: $(TARGET) $(TARGET_BIN) strip
	@echo "Build complete. Run 'make install' to install."

help:
	@echo "Usage: make [target]"
	@echo
	@echo "CLI targets:"
	@echo "  all             Build scriba CLI (default)"
	@echo "  static          Build with static libusb"
	@echo "  clean           Remove build artifacts"
	@echo "  install         Install scriba binary + udev rules"
	@echo "  install-rules   Install udev rules only"
	@echo
	@echo "Web / WASM targets:"
	@echo "  web             Build WASM module (frontends/web/public/wasm/)"
	@echo "  web-dist        Build WASM + Vite frontend (frontends/web/dist/)"
	@echo "  web-serve       Build + serve dist on http://localhost:8080"

	@echo
	@echo "Debug:"
	@echo "  CONFIG_STATIC=yes  Build with static libusb"

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

#
# Web / WASM build
#
WEB_DIR        = frontends/web
EMSDK_DIR      = emsdk
EMCC           = $(EMSDK_DIR)/upstream/emscripten/emcc
WEB_BUILD_DIR  = $(WEB_DIR)/build
WEB_OUT_DIR    = $(WEB_DIR)/public/wasm
WEB_JS_LIB     = $(WEB_DIR)/src/libusb-webusb.js
WEB_ASYNCIFY_JSON = $(WEB_BUILD_DIR)/asyncify_imports.json

WEB_CFLAGS  = -std=gnu99 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -O2
WEB_CFLAGS += -DGIT_COMMIT_DATE=\"web\" -DGIT_COMMIT_HASH=\"wasm\"
WEB_CFLAGS += -I$(WEB_DIR)/libusb-stub -I$(SRC_DIR)

WEB_SRCS = $(SRC_DIR)/flashcmd_api.c \
           $(SRC_DIR)/spi_controller.c \
           $(SRC_DIR)/spi_nand_flash.c \
           $(SRC_DIR)/spi_nand_flash_protocol.c \
           $(SRC_DIR)/spi_nand_flash_tables.c \
           $(SRC_DIR)/spi_nor_flash.c \
           $(SRC_DIR)/spi_nor_flash_tables.c \
           $(SRC_DIR)/ch341a_spi.c \
           $(SRC_DIR)/ezp2019_spi.c \
           $(SRC_DIR)/scriba.c \
           $(SRC_DIR)/usb_hal_webusb.c \
           $(SRC_DIR)/timer.c

WEB_EXPORTED_FUNCTIONS = _scriba_init,_scriba_init_programmer,_scriba_detect_chip,_scriba_get_flash_size,_scriba_get_chip_name,_scriba_get_programmer_type,_scriba_get_block_size,_scriba_read_flash,_scriba_write_flash,_scriba_erase_flash,_scriba_reinit,_scriba_shutdown,_scriba_get_version,_malloc,_free

WEB_EXPORTED_RUNTIME = ccall,cwrap,UTF8ToString,stringToUTF8,HEAPU8,HEAPU32,FS

WEB_LDFLAGS  = -s ASYNCIFY=1
WEB_LDFLAGS += -s ASYNCIFY_IMPORTS=@$(WEB_ASYNCIFY_JSON)
WEB_LDFLAGS += -s EXPORTED_FUNCTIONS=[$(WEB_EXPORTED_FUNCTIONS)]
WEB_LDFLAGS += -s EXPORTED_RUNTIME_METHODS=[$(WEB_EXPORTED_RUNTIME)]
WEB_LDFLAGS += -s ALLOW_MEMORY_GROWTH=1
WEB_LDFLAGS += -s MODULARIZE=1
WEB_LDFLAGS += -s EXPORT_NAME=createScribaModule
WEB_LDFLAGS += -s ASYNCIFY_STACK_SIZE=131072
WEB_LDFLAGS += -s ASSERTIONS=1
WEB_LDFLAGS += -s INITIAL_MEMORY=67108864
WEB_LDFLAGS += -s STACK_SIZE=8388608
WEB_LDFLAGS += --js-library $(WEB_JS_LIB)

WEB_OUT_JS  = $(WEB_OUT_DIR)/scriba.js
WEB_OUT_WASM = $(WEB_OUT_DIR)/scriba.wasm

# Auto-init emsdk submodule and install Emscripten on first build.
$(EMSDK_DIR):
	@echo "Initialising emsdk submodule..."
	git submodule update --init $(EMSDK_DIR)

$(EMCC): | $(EMSDK_DIR)
	@echo "Emscripten SDK not yet installed. Running installer..."
	cd $(EMSDK_DIR) && ./emsdk install latest
	cd $(EMSDK_DIR) && ./emsdk activate latest
	@echo "Emscripten SDK installed: $(EMCC)"

check-emsdk: $(EMCC)
	@:

$(WEB_ASYNCIFY_JSON):
	@mkdir -p $(WEB_BUILD_DIR)
	@printf '["emscripten_sleep","libusb_get_device_list","libusb_open","libusb_open_device_with_vid_pid","libusb_set_configuration","libusb_claim_interface","libusb_release_interface","libusb_control_transfer","libusb_bulk_transfer","libusb_interrupt_transfer","libusb_reset_device","usb_clear_halt"]' > $@

$(WEB_OUT_JS) $(WEB_OUT_WASM): $(WEB_SRCS) $(WEB_JS_LIB) $(WEB_ASYNCIFY_JSON) | check-emsdk
	@echo "Building scriba WASM module..."
	@mkdir -p $(WEB_OUT_DIR)
	$(EMCC) $(WEB_CFLAGS) $(WEB_SRCS) $(WEB_LDFLAGS) -o $(WEB_OUT_JS)
	@echo "WASM build complete: $(WEB_OUT_JS)"

.PHONY: web
web: $(WEB_OUT_JS) $(WEB_OUT_WASM)

.PHONY: web-dist
web-dist: web
	@echo "Installing npm dependencies..."
	@cd $(WEB_DIR) && [ -d node_modules ] || npm install --silent
	@echo "Building Vite frontend..."
	@cd $(WEB_DIR) && npm run build
	@echo "Web frontend built: $(WEB_DIR)/dist/"

.PHONY: web-serve
web-serve: web-dist
	@if python3 -c "import socket; s=socket.socket(); s.settimeout(1); s.connect(('localhost',8080)); s.close(); exit(0)" 2>/dev/null; then \
		echo "ERROR: Port 8080 is already in use. Stop the existing server first."; \
		exit 1; \
	fi
	@echo "Open http://localhost:8080 in Chrome/Edge"
	cd $(WEB_DIR)/dist && python3 -m http.server 8080

clean:
	rm -rf $(BUILD_DIR) $(WEB_BUILD_DIR) $(WEB_OUT_DIR) $(WEB_DIR)/dist

strip: $(TARGET_BIN)
	$(STRIP) $(TARGET_BIN)

install: $(TARGET_BIN) install-rules
	$(INSTALL) -m 0755 -D $(TARGET_BIN) $(DESTDIR)$(BINDIR)/scriba
	@echo "Install complete."

install-rules:
	$(INSTALL) -m 0664 -D resources/udev/40-persistent-ch341a.rules $(DESTDIR)/etc/udev/rules.d/40-persistent-ch341a.rules
	$(INSTALL) -m 0664 -D resources/udev/40-persistent-ezp2019.rules $(DESTDIR)/etc/udev/rules.d/40-persistent-ezp2019.rules
	@if [ -z "$(DESTDIR)" ]; then \
		echo "Reloading udev rules..."; \
		udevadm control --reload-rules 2>/dev/null || true; \
		udevadm trigger 2>/dev/null || true; \
		echo "Done. Unplug and replug your programmer if already connected."; \
	fi

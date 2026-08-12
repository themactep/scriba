# Scriba Web — WASM + frontend build
# Just run: make

EMCC      ?= emcc
EMSDK_DIR ?= $(CURDIR)/emsdk
NPM       ?= npm
NPROC     := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

SRC_DIR   := $(CURDIR)/../src
WEB_SRC   := $(CURDIR)/src
STUB_DIR  := $(CURDIR)/libusb-stub
OUT_DIR   := $(CURDIR)/public/wasm
BUILD     := $(shell date +%Y%m%d-%H%M%S)

SRCS := $(SRC_DIR)/flashcmd_api.c \
	$(SRC_DIR)/spi_controller.c \
	$(SRC_DIR)/spi_nand_flash.c \
	$(SRC_DIR)/spi_nand_flash_protocol.c \
	$(SRC_DIR)/spi_nand_flash_tables.c \
	$(SRC_DIR)/spi_nor_flash.c \
	$(SRC_DIR)/ch341a_spi.c \
	$(SRC_DIR)/ezp2019_spi.c \
	$(SRC_DIR)/timer.c \
	$(WEB_SRC)/web_main.c

CFLAGS := -std=gnu99 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable \
	-O3 -DNDEBUG \
	-DGIT_COMMIT_DATE=\"web\" \
	-DGIT_COMMIT_HASH=\"wasm\" \
	-DSCRIBA_WASM_BUILD=\"$(BUILD)\" \
	-I$(STUB_DIR) -I$(SRC_DIR)

# Async libusb functions — keep in sync with libusb-webusb.js
ASYNCIFY_IMPORTS := emscripten_sleep,libusb_get_device_list,libusb_open,libusb_open_device_with_vid_pid,libusb_set_configuration,libusb_claim_interface,libusb_release_interface,libusb_control_transfer,libusb_bulk_transfer,libusb_interrupt_transfer,libusb_reset_device,usb_clear_halt

EXPORTS := _main,_scriba_init,_scriba_init_programmer,_scriba_detect_chip,_scriba_get_flash_size,_scriba_get_chip_name,_scriba_get_programmer_type,_scriba_get_block_size,_scriba_read_flash,_scriba_write_flash,_scriba_erase_flash,_scriba_reinit,_scriba_shutdown,_scriba_get_version,_malloc,_free

RUNTIME := ccall,cwrap,UTF8ToString,stringToUTF8,HEAPU8,HEAPU32,FS

EMCC_FLAGS := \
	-s ASYNCIFY=1 \
	-s "ASYNCIFY_IMPORTS=$(ASYNCIFY_IMPORTS)" \
	-s "EXPORTED_FUNCTIONS=[$(EXPORTS)]" \
	-s "EXPORTED_RUNTIME_METHODS=[$(RUNTIME)]" \
	-s ALLOW_MEMORY_GROWTH=1 \
	-s MODULARIZE=1 \
	-s "EXPORT_NAME=createScribaModule" \
	-s ASYNCIFY_STACK_SIZE=131072 \
	-s ASSERTIONS=1 \
	-s INITIAL_MEMORY=67108864 \
	-s STACK_SIZE=8388608 \
	--js-library $(WEB_SRC)/libusb-webusb.js

TARGET := $(OUT_DIR)/scriba.js

.PHONY: all clean serve

all: $(TARGET) frontend
	@echo "Build complete. Output in $(CURDIR)/dist/"
	@ls -la $(CURDIR)/dist/

# --- WASM ---
$(TARGET): $(SRCS) $(WEB_SRC)/libusb-webusb.js | $(OUT_DIR)
	$(EMCC) $(CFLAGS) $(SRCS) $(EMCC_FLAGS) -o $@

$(OUT_DIR):
	mkdir -p $@

# --- Emscripten bootstrap ---
# If emcc not on PATH, look in EMSDK_DIR or auto-install.
_EMSDK_ENV := $(EMSDK_DIR)/emsdk_env.sh
ifeq (,$(shell command -v $(EMCC) 2>/dev/null))
  ifneq (,$(wildcard $(_EMSDK_ENV)))
    EMCC := $(shell . '$(_EMSDK_ENV)' >/dev/null 2>&1 && command -v emcc)
  endif
endif

.PHONY: _emsdk_install
_emsdk_install:
ifeq (,$(shell command -v $(EMCC) 2>/dev/null))
  ifeq (,$(wildcard $(_EMSDK_ENV)))
	git clone --depth 1 https://github.com/emscripten-core/emsdk.git "$(EMSDK_DIR)"
	cd "$(EMSDK_DIR)" && ./emsdk install latest && ./emsdk activate latest
  endif
	@echo "Emscripten installed at $(EMSDK_DIR). Run 'make' again."
	@exit 1
endif

$(TARGET): | _emsdk_install

# --- Frontend ---
frontend: node_modules
	$(NPM) run build

node_modules:
	$(NPM) install

# --- Clean ---
clean:
	rm -rf $(OUT_DIR) $(CURDIR)/dist $(CURDIR)/build

distclean: clean
	rm -rf node_modules

serve:
	$(NPM) run dev

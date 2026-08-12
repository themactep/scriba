# Scriba Web — WASM + frontend build
# Just run: make

EMSDK_DIR ?= $(CURDIR)/emsdk
NPM       ?= npm
NPROC     := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

SRC_DIR   := $(CURDIR)/../src
WEB_SRC   := $(CURDIR)/src
STUB_DIR  := $(CURDIR)/libusb-stub
OUT_DIR   := $(CURDIR)/public/wasm
BUILD     := $(shell date +%Y%m%d-%H%M%S)

_BUNDLED   := $(EMSDK_DIR)/upstream/emscripten/emcc

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

.PHONY: all clean distclean serve

all: $(TARGET) frontend
	@echo "Build complete. Output in $(CURDIR)/dist/"
	@ls -la $(CURDIR)/dist/

# --- WASM ---
# If emcc is on PATH, use it. Otherwise use the bundled emsdk.
_EMCC := $(or $(shell command -v emcc 2>/dev/null),$(_BUNDLED))

$(TARGET): $(SRCS) $(WEB_SRC)/libusb-webusb.js | $(OUT_DIR) _emsdk
	$(_EMCC) $(CFLAGS) $(SRCS) $(EMCC_FLAGS) -o $@

$(OUT_DIR):
	mkdir -p $@

# --- Emscripten bootstrap ---
.PHONY: _emsdk
_emsdk:
	@if command -v emcc >/dev/null 2>&1; then true; \
	elif [ -x "$(_BUNDLED)" ]; then true; \
	elif [ -d "$(EMSDK_DIR)" ]; then \
		echo "emsdk found but not installed. Run: cd $(EMSDK_DIR) && ./emsdk install latest"; \
		exit 1; \
	else \
		echo "Downloading Emscripten SDK to $(EMSDK_DIR)..."; \
		git clone --depth 1 https://github.com/emscripten-core/emsdk.git "$(EMSDK_DIR)"; \
		cd "$(EMSDK_DIR)" && ./emsdk install latest && ./emsdk activate latest; \
		echo "Emscripten installed. Continuing build..."; \
	fi

# --- Frontend ---
frontend: node_modules
	$(NPM) run build

node_modules:
	$(NPM) install

# --- Clean ---
clean:
	rm -rf $(OUT_DIR) $(CURDIR)/dist $(CURDIR)/build

distclean: clean
	rm -rf node_modules $(EMSDK_DIR)

serve:
	$(NPM) run dev

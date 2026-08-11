cmake_minimum_required(VERSION 3.10)
project(scriba-web LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(LIBUSB_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/libusb-stub")

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")

set(SCRIBA_SOURCES
    ${SRC_DIR}/flashcmd_api.c
    ${SRC_DIR}/spi_controller.c
    ${SRC_DIR}/spi_nand_flash.c
    ${SRC_DIR}/spi_nand_flash_protocol.c
    ${SRC_DIR}/spi_nand_flash_tables.c
    ${SRC_DIR}/spi_nor_flash.c
    ${SRC_DIR}/ch341a_spi.c
    ${SRC_DIR}/ezp2019_spi.c
    ${SRC_DIR}/timer.c
    ${SRC_DIR}/ch341a_i2c.c
    ${SRC_DIR}/i2c_eeprom.c
    ${SRC_DIR}/spi_eeprom.c
    ${SRC_DIR}/bitbang_microwire.c
    ${SRC_DIR}/mw_eeprom.c
    ${SRC_DIR}/ch341a_gpio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/web_main.c
)

add_executable(scriba_web ${SCRIBA_SOURCES})

target_include_directories(scriba_web PRIVATE
    ${LIBUSB_INCLUDE_DIRS}
    ${SRC_DIR}
)

string(TIMESTAMP SCRIBA_WASM_BUILD "%Y%m%d-%H%M%S")
target_compile_definitions(scriba_web PRIVATE
    GIT_COMMIT_DATE="web"
    GIT_COMMIT_HASH="wasm"
    SCRIBA_WASM_BUILD="${SCRIBA_WASM_BUILD}"
    EEPROM_SUPPORT
)

target_compile_options(scriba_web PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable)

# ASYNCIFY_IMPORTS: every async libusb function in libusb-webusb.js MUST be
# listed here or the WASM build hangs at runtime. When adding USB calls,
# update both this list AND libusb-webusb.js simultaneously.
 file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/asyncify_imports.json"
    "[\"emscripten_sleep\",\"libusb_get_device_list\",\"libusb_open\",\"libusb_open_device_with_vid_pid\",\"libusb_set_configuration\",\"libusb_claim_interface\",\"libusb_release_interface\",\"libusb_control_transfer\",\"libusb_bulk_transfer\",\"libusb_interrupt_transfer\",\"libusb_reset_device\",\"usb_clear_halt\"]"
 )

set(EXPORTED_FUNCTIONS
    "_main"
    "_scriba_init"
    "_scriba_init_programmer"
    "_scriba_detect_chip"
    "_scriba_get_flash_size"
    "_scriba_get_chip_name"
    "_scriba_get_programmer_type"
    "_scriba_get_block_size"
    "_scriba_read_flash"
    "_scriba_write_flash"
    "_scriba_erase_flash"
    "_scriba_reinit"
    "_scriba_shutdown"
    "_scriba_get_version"
    "_malloc"
    "_free"
)
list(JOIN EXPORTED_FUNCTIONS "," EXPORTED_FUNCS_STR)

target_link_options(scriba_web PRIVATE
    "SHELL:-s ASYNCIFY=1"
    "SHELL:-s ASYNCIFY_IMPORTS=@${CMAKE_CURRENT_BINARY_DIR}/asyncify_imports.json"
    "SHELL:-s EXPORTED_FUNCTIONS=[${EXPORTED_FUNCS_STR}]"
    "SHELL:-s EXPORTED_RUNTIME_METHODS=[\"ccall\",\"cwrap\",\"UTF8ToString\",\"stringToUTF8\",\"HEAPU8\",\"HEAPU32\",\"FS\"]"
    "SHELL:-s ALLOW_MEMORY_GROWTH=1"
    "SHELL:-s MODULARIZE=1"
    "SHELL:-s EXPORT_NAME=createScribaModule"
    "SHELL:-s ASYNCIFY_STACK_SIZE=131072"
    "SHELL:-s ASSERTIONS=1"
    # 64 MB initial heap — enough for ~16 MB flash read buffers plus overhead.
    # ALLOW_MEMORY_GROWTH=1 handles larger chips, but growth is slow and may
    # trigger browser OOM on 32-bit. Practical limit: ~128 MB flash.
    "SHELL:-s INITIAL_MEMORY=67108864"
    "SHELL:-s STACK_SIZE=8388608"
    "SHELL:--js-library ${CMAKE_CURRENT_SOURCE_DIR}/src/libusb-webusb.js"
)

set_target_properties(scriba_web PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/public/wasm"
    OUTPUT_NAME "scriba"
    SUFFIX ".js"
)

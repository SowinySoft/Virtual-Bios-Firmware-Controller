cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico CACHE STRING "Board type")

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(vbfc_controller C CXX ASM)

set(CMAKE_C_STANDARD 11)

pico_sdk_init()

option(VBFC_SIM "Build with simulated SPI (no hardware arbiter)" OFF)

add_executable(vbfc_controller
    src/main.c
    src/spi_arbiter.c
    src/shadow_map.c
    src/patch_table.c
    src/orig_flash.c
    src/ext_flash.c
    src/config_store.c
    src/b64.c
    src/vbfc_crc.c
    src/vbfc_sha256.c
    src/image_check.c
    src/proto_binary.c
    src/sniffer.c
    src/usb_service.c
    src/safety.c
)

target_include_directories(vbfc_controller PRIVATE include)

target_link_libraries(vbfc_controller
    pico_stdlib
    pico_multicore
    hardware_spi
    hardware_gpio
    hardware_i2c
    hardware_watchdog
)

pico_enable_stdio_usb(vbfc_controller 1)
pico_enable_stdio_uart(vbfc_controller 0)

# The binary protocol emits ~1 KB base64 lines per DUMP CHUNK. The default USB
# CDC stdio buffer (256 B) would stall between line fragments, so widen it.
target_compile_definitions(vbfc_controller PRIVATE
    PICO_STDIO_USB_CDC_SUPPORT_RATE_CHANGE_REQUEST=0
    PICO_USB_HOST_TASK_ALWAYS=1
)

if(VBFC_SIM)
    target_compile_definitions(vbfc_controller PRIVATE VBFC_SIM=1)
endif()

pico_add_extra_outputs(vbfc_controller)

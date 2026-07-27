cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico CACHE STRING "Board type")
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(vbfc_test_master C CXX ASM)

set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(vbfc_test_master
    src/main.c
)

target_link_libraries(vbfc_test_master pico_stdlib hardware_spi hardware_gpio)
pico_enable_stdio_usb(vbfc_test_master 1)
pico_enable_stdio_uart(vbfc_test_master 0)
pico_add_extra_outputs(vbfc_test_master)

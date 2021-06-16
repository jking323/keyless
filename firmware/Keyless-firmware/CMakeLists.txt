# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# initalize pico_sdk from installed location
# (note this can come from environment, CMake cache etc)
set(PICO_SDK_PATH "/Users/jeremy/Documents/pico/pico-sdk")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(Keyless-firmware C CXX ASM)

set(PICO_CXX_ENABLE_EXCEPTIONS 1)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(Keyless-firmware Keyless-firmware.cpp ultra_wide.cpp ultra_wide.h keyless-firmware.h)

pico_set_program_name(Keyless-firmware "Keyless-firmware")
pico_set_program_version(Keyless-firmware "0.01a")

pico_enable_stdio_uart(Keyless-firmware 1)
pico_enable_stdio_usb(Keyless-firmware 0)

# Add the standard library to the build
target_link_libraries(Keyless-firmware pico_stdlib)

# Add any user requested libraries
target_link_libraries(Keyless-firmware
        hardware_spi
        hardware_watchdog
        multicore
        pico_multicore
        pico_stdlib
        hardware_adc
        )

pico_add_extra_outputs(Keyless-firmware)


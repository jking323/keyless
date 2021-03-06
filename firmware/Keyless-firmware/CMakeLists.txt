# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)


set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# initalize pico_sdk from installed location
# (note this can come from environment, CMake cache etc)
set(PICO_SDK_PATH "/Users/jeremy/Documents/pico/pico-sdk")

#include(uwb_dw1000/CMakeLists.txt)
# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(Keyless-firmware C CXX ASM)

set(PICO_CXX_ENABLE_EXCEPTIONS 1)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1
set(SOURCE_FILES
        Keyless-firmware.cpp
        user_verify.cpp
        core1.cpp
        input.h output.h catch2.h catch2.cpp)
#add_library(uwb_dw1000)

add_executable(Keyless-firmware ${SOURCE_FILES})

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
        pico_multicore
        pico_stdlib
        hardware_adc
        )

pico_add_extra_outputs(Keyless-firmware)


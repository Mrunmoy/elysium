cmake_minimum_required(VERSION 3.14)

project(ms-os
    LANGUAGES C CXX ASM
    VERSION 0.1.0
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Common warning flags
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)

# Detect cross-compilation vs host build
if(CMAKE_CROSSCOMPILING)
    # ---------- Cross-compilation (ARM firmware) ----------

    # Default target if not specified
    if(NOT DEFINED MSOS_TARGET)
        set(MSOS_TARGET "stm32f207zgt6")
    endif()

    # Target-specific configuration
    if(MSOS_TARGET STREQUAL "stm32f407vet6")
        add_compile_definitions(STM32F407xx)
        set(MSOS_STARTUP_DIR stm32f407vet6)
        set(MSOS_ARCH_DIR cortex-m4)
        set(MSOS_CMSIS_DEVICE_DIR ${CMAKE_SOURCE_DIR}/vendor/cmsis-device-f4/Include)
        set(MSOS_LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/startup/stm32f407vet6/Linker.ld)
    else()
        add_compile_definitions(STM32F207xx)
        set(MSOS_STARTUP_DIR stm32f207zgt6)
        set(MSOS_ARCH_DIR cortex-m3)
        set(MSOS_CMSIS_DEVICE_DIR ${CMAKE_SOURCE_DIR}/vendor/cmsis-device-f2/Include)
        set(MSOS_LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/startup/stm32f207zgt6/Linker.ld)
    endif()

    # Prevent compiler from generating hardware FP instructions in libraries
    # that would conflict with our soft/hard float settings
    add_compile_options(
        -ffunction-sections
        -fdata-sections
        -fno-common
    )

    # CMSIS headers (interface-only target)
    add_library(cmsis INTERFACE)
    target_include_directories(cmsis INTERFACE
        ${CMAKE_SOURCE_DIR}/vendor/cmsis_core/CMSIS/Core/Include
        ${MSOS_CMSIS_DEVICE_DIR}
    )

    add_subdirectory(startup)
    add_subdirectory(hal)
    add_subdirectory(kernel)
    add_subdirectory(app/blinky)
    add_subdirectory(app/threads)

else()
    # ---------- Host build (unit tests) ----------

    enable_testing()

    add_subdirectory(test/vendor/googletest)
    add_subdirectory(test)
endif()

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

    add_compile_definitions(
        STM32F207xx
    )

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
        ${CMAKE_SOURCE_DIR}/vendor/cmsis-device-f2/Include
    )

    add_subdirectory(startup)
    add_subdirectory(hal)
    add_subdirectory(app/blinky)

else()
    # ---------- Host build (unit tests) ----------

    enable_testing()

    add_subdirectory(test/vendor/googletest)
    add_subdirectory(test)
endif()

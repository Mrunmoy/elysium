add_library(startup STATIC
    stm32f407vgt6/Startup.s
    stm32f407vgt6/SystemInit.cpp
)

target_include_directories(startup PRIVATE
    ${CMAKE_SOURCE_DIR}/hal/inc/hal
    ${CMAKE_SOURCE_DIR}/vendor/cmsis-device-f4/Include
    ${CMAKE_SOURCE_DIR}/vendor/cmsis_core/CMSIS/Core/Include
)

target_compile_options(startup PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti -fno-unwind-tables>
)

set_source_files_properties(stm32f407vgt6/Startup.s PROPERTIES
    LANGUAGE ASM
)

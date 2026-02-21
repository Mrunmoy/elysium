add_library(startup STATIC
    ${MSOS_STARTUP_DIR}/Startup.s
    ${MSOS_STARTUP_DIR}/SystemInit.cpp
)

target_include_directories(startup PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
)

if(MSOS_CMSIS_DEVICE_DIR)
    target_include_directories(startup PRIVATE
        ${CMAKE_SOURCE_DIR}/hal/inc/hal
        ${MSOS_CMSIS_DEVICE_DIR}
        ${CMAKE_SOURCE_DIR}/vendor/cmsis_core/CMSIS/Core/Include
    )
endif()

target_compile_options(startup PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti -fno-unwind-tables>
)

set_source_files_properties(${MSOS_STARTUP_DIR}/Startup.s PROPERTIES
    LANGUAGE ASM
)

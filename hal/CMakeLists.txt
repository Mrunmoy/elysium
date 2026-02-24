if(MSOS_HAL_DIR)
    set(HAL_SRC_DIR src/${MSOS_HAL_DIR})
else()
    set(HAL_SRC_DIR src/stm32f4)
endif()

add_library(hal STATIC
    ${HAL_SRC_DIR}/Gpio.cpp
    ${HAL_SRC_DIR}/Uart.cpp
    ${HAL_SRC_DIR}/Rcc.cpp
    ${HAL_SRC_DIR}/Watchdog.cpp
)

target_include_directories(hal PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
)

target_link_libraries(hal PRIVATE startup)

target_compile_options(hal PRIVATE
    -fno-exceptions
    -fno-rtti
    -fno-unwind-tables
)

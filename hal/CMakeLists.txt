add_library(hal STATIC
    src/stm32f4/Gpio.cpp
    src/stm32f4/Uart.cpp
    src/stm32f4/Rcc.cpp
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

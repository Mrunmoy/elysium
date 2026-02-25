add_executable(spi-demo
    main.cpp
)

target_link_libraries(spi-demo PRIVATE
    kernel
    hal
    startup
    board_dtb
)

target_compile_options(spi-demo PRIVATE
    -fno-exceptions
    -fno-rtti
    -fno-unwind-tables
)

target_link_options(spi-demo PRIVATE
    -T${MSOS_LINKER_SCRIPT}
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    --specs=nosys.specs
    --specs=nano.specs
    -nostartfiles
)

# Generate .bin and .hex from .elf
add_custom_command(TARGET spi-demo POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:spi-demo> ${CMAKE_CURRENT_BINARY_DIR}/spi-demo.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:spi-demo> ${CMAKE_CURRENT_BINARY_DIR}/spi-demo.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:spi-demo>
    COMMENT "Generating spi-demo.bin and spi-demo.hex"
)

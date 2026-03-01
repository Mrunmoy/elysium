add_executable(spi2-test
    main.cpp
)

target_link_libraries(spi2-test PRIVATE
    kernel
    hal
    startup
    board_dtb
)

target_compile_options(spi2-test PRIVATE
    -fno-exceptions
    -fno-rtti
    -fno-unwind-tables
)

target_link_options(spi2-test PRIVATE
    -T${MSOS_LINKER_SCRIPT}
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    --specs=nosys.specs
    --specs=nano.specs
    -nostartfiles
)

# Generate .bin and .hex from .elf
add_custom_command(TARGET spi2-test POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:spi2-test> ${CMAKE_CURRENT_BINARY_DIR}/spi2-test.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:spi2-test> ${CMAKE_CURRENT_BINARY_DIR}/spi2-test.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:spi2-test>
    COMMENT "Generating spi2-test.bin and spi2-test.hex"
)

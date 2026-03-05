add_executable(i2c-test
    main.cpp
)

target_link_libraries(i2c-test PRIVATE
    kernel
    hal
    startup
    board_dtb
)

target_compile_options(i2c-test PRIVATE
    -fno-exceptions
    -fno-rtti
    -fno-unwind-tables
)

target_link_options(i2c-test PRIVATE
    -T${MSOS_LINKER_SCRIPT}
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    --specs=nosys.specs
    --specs=nano.specs
    -nostartfiles
)

# Generate .bin and .hex from .elf
add_custom_command(TARGET i2c-test POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:i2c-test> ${CMAKE_CURRENT_BINARY_DIR}/i2c-test.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:i2c-test> ${CMAKE_CURRENT_BINARY_DIR}/i2c-test.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:i2c-test>
    COMMENT "Generating i2c-test.bin and i2c-test.hex"
)

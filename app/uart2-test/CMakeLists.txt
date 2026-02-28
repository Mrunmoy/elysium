add_executable(uart2-test
    main.cpp
)

target_link_libraries(uart2-test PRIVATE
    kernel
    hal
    startup
    board_dtb
)

target_compile_options(uart2-test PRIVATE
    -fno-exceptions
    -fno-rtti
    -fno-unwind-tables
)

target_link_options(uart2-test PRIVATE
    -T${MSOS_LINKER_SCRIPT}
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    --specs=nosys.specs
    --specs=nano.specs
    -nostartfiles
)

# Generate .bin and .hex from .elf
add_custom_command(TARGET uart2-test POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:uart2-test> ${CMAKE_CURRENT_BINARY_DIR}/uart2-test.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:uart2-test> ${CMAKE_CURRENT_BINARY_DIR}/uart2-test.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:uart2-test>
    COMMENT "Generating uart2-test.bin and uart2-test.hex"
)

add_executable(uart2-echo
    main.cpp
)

target_link_libraries(uart2-echo PRIVATE
    kernel
    hal
    startup
    board_dtb
)

target_compile_options(uart2-echo PRIVATE
    -fno-exceptions
    -fno-rtti
    -fno-unwind-tables
)

target_link_options(uart2-echo PRIVATE
    -T${MSOS_LINKER_SCRIPT}
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    --specs=nosys.specs
    --specs=nano.specs
    -nostartfiles
)

# Generate .bin and .hex from .elf
add_custom_command(TARGET uart2-echo POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:uart2-echo> ${CMAKE_CURRENT_BINARY_DIR}/uart2-echo.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:uart2-echo> ${CMAKE_CURRENT_BINARY_DIR}/uart2-echo.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:uart2-echo>
    COMMENT "Generating uart2-echo.bin and uart2-echo.hex"
)

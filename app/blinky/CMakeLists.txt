add_executable(blinky
    main.cpp
)

target_link_libraries(blinky PRIVATE
    kernel
    hal
    startup
    board_dtb
)

target_compile_options(blinky PRIVATE
    -fno-exceptions
    -fno-rtti
    -fno-unwind-tables
)

target_link_options(blinky PRIVATE
    -T${MSOS_LINKER_SCRIPT}
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    --specs=nosys.specs
    --specs=nano.specs
    -nostartfiles
)

# Generate .bin and .hex from .elf
add_custom_command(TARGET blinky POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:blinky> ${CMAKE_CURRENT_BINARY_DIR}/blinky.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:blinky> ${CMAKE_CURRENT_BINARY_DIR}/blinky.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:blinky>
    COMMENT "Generating blinky.bin and blinky.hex"
)

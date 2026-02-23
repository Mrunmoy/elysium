add_executable(ipc-demo
    main.cpp
)

target_link_libraries(ipc-demo PRIVATE
    -Wl,--whole-archive kernel -Wl,--no-whole-archive
    hal
    startup
    board_dtb
)

target_compile_options(ipc-demo PRIVATE
    -fno-exceptions
    -fno-rtti
    -fno-unwind-tables
)

target_link_options(ipc-demo PRIVATE
    -T${MSOS_LINKER_SCRIPT}
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    --specs=nosys.specs
    --specs=nano.specs
    -nostartfiles
)

# Generate .bin and .hex from .elf
add_custom_command(TARGET ipc-demo POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:ipc-demo> ${CMAKE_CURRENT_BINARY_DIR}/ipc-demo.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:ipc-demo> ${CMAKE_CURRENT_BINARY_DIR}/ipc-demo.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:ipc-demo>
    COMMENT "Generating ipc-demo.bin and ipc-demo.hex"
)

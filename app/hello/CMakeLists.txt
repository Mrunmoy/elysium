add_executable(hello
    main.cpp
)

target_link_libraries(hello PRIVATE
    hal
    startup
    board_config
)

target_compile_options(hello PRIVATE
    -fno-exceptions
    -fno-rtti
    -fno-unwind-tables
)

target_link_options(hello PRIVATE
    -T${MSOS_LINKER_SCRIPT}
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    --specs=nosys.specs
    --specs=nano.specs
    -nostartfiles
)

# Generate .bin and .hex from .elf
add_custom_command(TARGET hello POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:hello> ${CMAKE_CURRENT_BINARY_DIR}/hello.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:hello> ${CMAKE_CURRENT_BINARY_DIR}/hello.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:hello>
    COMMENT "Generating hello.bin and hello.hex"
)

add_executable(threads
    main.cpp
)

target_link_libraries(threads PRIVATE
    kernel
    hal
    startup
)

target_compile_options(threads PRIVATE
    -fno-exceptions
    -fno-rtti
    -fno-unwind-tables
)

target_link_options(threads PRIVATE
    -T${CMAKE_SOURCE_DIR}/startup/stm32f207zgt6/Linker.ld
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    --specs=nosys.specs
    --specs=nano.specs
    -nostartfiles
)

# Generate .bin and .hex from .elf
add_custom_command(TARGET threads POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:threads> ${CMAKE_CURRENT_BINARY_DIR}/threads.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:threads> ${CMAKE_CURRENT_BINARY_DIR}/threads.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:threads>
    COMMENT "Generating threads.bin and threads.hex"
)

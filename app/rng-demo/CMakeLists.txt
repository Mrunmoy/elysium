add_executable(rng-demo
    main.cpp
)

target_link_libraries(rng-demo PRIVATE
    kernel
    hal
    startup
    board_dtb
)

target_compile_options(rng-demo PRIVATE
    -fno-exceptions
    -fno-rtti
    -fno-unwind-tables
)

target_link_options(rng-demo PRIVATE
    -T${MSOS_LINKER_SCRIPT}
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    --specs=nosys.specs
    --specs=nano.specs
    -nostartfiles
)

add_custom_command(TARGET rng-demo POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:rng-demo> ${CMAKE_CURRENT_BINARY_DIR}/rng-demo.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:rng-demo> ${CMAKE_CURRENT_BINARY_DIR}/rng-demo.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:rng-demo>
    COMMENT "Generating rng-demo.bin and rng-demo.hex"
)

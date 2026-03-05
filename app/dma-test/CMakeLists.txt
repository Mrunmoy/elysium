add_executable(dma-test
    main.cpp
)

target_link_libraries(dma-test PRIVATE
    kernel
    hal
    startup
    board_dtb
)

target_compile_options(dma-test PRIVATE
    -fno-exceptions
    -fno-rtti
    -fno-unwind-tables
)

target_link_options(dma-test PRIVATE
    -T${MSOS_LINKER_SCRIPT}
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    --specs=nosys.specs
    --specs=nano.specs
    -nostartfiles
)

add_custom_command(TARGET dma-test POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:dma-test> ${CMAKE_CURRENT_BINARY_DIR}/dma-test.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:dma-test> ${CMAKE_CURRENT_BINARY_DIR}/dma-test.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:dma-test>
    COMMENT "Generating dma-test.bin and dma-test.hex"
)

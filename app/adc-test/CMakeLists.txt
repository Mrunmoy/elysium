add_executable(adc-test
    main.cpp
)

target_link_libraries(adc-test PRIVATE
    kernel
    hal
    startup
    board_dtb
)

target_compile_options(adc-test PRIVATE
    -fno-exceptions
    -fno-rtti
    -fno-unwind-tables
)

target_link_options(adc-test PRIVATE
    -T${MSOS_LINKER_SCRIPT}
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    --specs=nosys.specs
    --specs=nano.specs
    -nostartfiles
)

add_custom_command(TARGET adc-test POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:adc-test> ${CMAKE_CURRENT_BINARY_DIR}/adc-test.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:adc-test> ${CMAKE_CURRENT_BINARY_DIR}/adc-test.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:adc-test>
    COMMENT "Generating adc-test.bin and adc-test.hex"
)

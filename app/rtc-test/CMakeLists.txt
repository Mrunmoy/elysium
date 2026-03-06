add_executable(rtc-test
    main.cpp
)

target_link_libraries(rtc-test PRIVATE
    kernel
    hal
    startup
    board_dtb
)

target_compile_options(rtc-test PRIVATE
    -fno-exceptions
    -fno-rtti
    -fno-unwind-tables
)

target_link_options(rtc-test PRIVATE
    -T${MSOS_LINKER_SCRIPT}
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    --specs=nosys.specs
    --specs=nano.specs
    -nostartfiles
)

add_custom_command(TARGET rtc-test POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:rtc-test> ${CMAKE_CURRENT_BINARY_DIR}/rtc-test.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:rtc-test> ${CMAKE_CURRENT_BINARY_DIR}/rtc-test.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:rtc-test>
    COMMENT "Generating rtc-test.bin and rtc-test.hex"
)

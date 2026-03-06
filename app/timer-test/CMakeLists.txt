add_executable(timer-test
    main.cpp
)

target_link_libraries(timer-test PRIVATE
    kernel
    hal
    startup
    board_dtb
)

target_compile_options(timer-test PRIVATE
    -fno-exceptions
    -fno-rtti
    -fno-unwind-tables
)

target_link_options(timer-test PRIVATE
    -T${MSOS_LINKER_SCRIPT}
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    --specs=nosys.specs
    --specs=nano.specs
    -nostartfiles
)

add_custom_command(TARGET timer-test POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:timer-test> ${CMAKE_CURRENT_BINARY_DIR}/timer-test.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:timer-test> ${CMAKE_CURRENT_BINARY_DIR}/timer-test.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:timer-test>
    COMMENT "Generating timer-test.bin and timer-test.hex"
)

add_executable(timer-logic
    logic-test.cpp
)

target_link_libraries(timer-logic PRIVATE
    kernel
    hal
    startup
    board_dtb
)

target_compile_options(timer-logic PRIVATE
    -fno-exceptions
    -fno-rtti
    -fno-unwind-tables
)

target_link_options(timer-logic PRIVATE
    -T${MSOS_LINKER_SCRIPT}
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    --specs=nosys.specs
    --specs=nano.specs
    -nostartfiles
)

add_custom_command(TARGET timer-logic POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:timer-logic> ${CMAKE_CURRENT_BINARY_DIR}/timer-logic.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:timer-logic> ${CMAKE_CURRENT_BINARY_DIR}/timer-logic.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:timer-logic>
    COMMENT "Generating timer-logic.bin and timer-logic.hex"
)

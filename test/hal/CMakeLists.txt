add_executable(hal_tests
    GpioTest.cpp
    WatchdogTest.cpp
    UartTest.cpp
    MockGpio.cpp
    MockWatchdog.cpp
    MockUart.cpp
)

target_include_directories(hal_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/hal/inc
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(hal_tests PRIVATE
    GTest::gtest_main
)

gtest_discover_tests(hal_tests)

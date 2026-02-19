add_executable(kernel_tests
    ThreadTest.cpp
    SchedulerTest.cpp
    MockCortexM.cpp
    MockCrashDump.cpp
    MockKernelGlobals.cpp
    ${CMAKE_SOURCE_DIR}/kernel/src/core/Thread.cpp
    ${CMAKE_SOURCE_DIR}/kernel/src/core/Scheduler.cpp
)

target_include_directories(kernel_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/kernel/inc
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(kernel_tests PRIVATE
    GTest::gtest_main
)

gtest_discover_tests(kernel_tests)

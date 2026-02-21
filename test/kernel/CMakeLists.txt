add_executable(kernel_tests
    ThreadTest.cpp
    SchedulerTest.cpp
    MutexTest.cpp
    SemaphoreTest.cpp
    CrashDumpTest.cpp
    BlockPoolTest.cpp
    HeapTest.cpp
    MpuTest.cpp
    MockArch.cpp
    MockCrashDump.cpp
    MockKernelGlobals.cpp
    MockMpu.cpp
    ${CMAKE_SOURCE_DIR}/kernel/src/core/Thread.cpp
    ${CMAKE_SOURCE_DIR}/kernel/src/core/Scheduler.cpp
    ${CMAKE_SOURCE_DIR}/kernel/src/core/Mutex.cpp
    ${CMAKE_SOURCE_DIR}/kernel/src/core/Semaphore.cpp
    ${CMAKE_SOURCE_DIR}/kernel/src/core/WaitQueue.cpp
    ${CMAKE_SOURCE_DIR}/kernel/src/core/CrashDumpCommon.cpp
    ${CMAKE_SOURCE_DIR}/kernel/src/core/BlockPool.cpp
    ${CMAKE_SOURCE_DIR}/kernel/src/core/Heap.cpp
)

target_include_directories(kernel_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/kernel/inc
    ${CMAKE_SOURCE_DIR}/kernel/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(kernel_tests PRIVATE
    GTest::gtest_main
)

gtest_discover_tests(kernel_tests)

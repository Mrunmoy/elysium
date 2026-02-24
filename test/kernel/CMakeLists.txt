add_executable(kernel_tests
    ThreadTest.cpp
    SchedulerTest.cpp
    MutexTest.cpp
    SemaphoreTest.cpp
    CrashDumpTest.cpp
    BlockPoolTest.cpp
    HeapTest.cpp
    MpuTest.cpp
    IpcTest.cpp
    PowerTest.cpp
    ShellTest.cpp
    FdtTest.cpp
    BoardConfigTest.cpp
    SvcDispatchTest.cpp
    SyscallTest.cpp
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
    ${CMAKE_SOURCE_DIR}/kernel/src/core/Ipc.cpp
    ${CMAKE_SOURCE_DIR}/kernel/src/core/Shell.cpp
    ${CMAKE_SOURCE_DIR}/kernel/src/core/Fdt.cpp
    ${CMAKE_SOURCE_DIR}/kernel/src/core/BoardConfig.cpp
    ${CMAKE_SOURCE_DIR}/kernel/src/core/SvcDispatch.cpp
)

target_include_directories(kernel_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/kernel/inc
    ${CMAKE_SOURCE_DIR}/kernel/src/core
    ${CMAKE_SOURCE_DIR}/hal/inc
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(kernel_tests PRIVATE
    MSOS_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    MSOS_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    MSOS_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

target_link_libraries(kernel_tests PRIVATE
    GTest::gtest_main
)

gtest_discover_tests(kernel_tests)

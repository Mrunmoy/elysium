add_library(kernel STATIC
    src/core/Thread.cpp
    src/core/Scheduler.cpp
    src/core/Kernel.cpp
    src/core/CrashDumpCommon.cpp
    src/arch/${MSOS_ARCH_DIR}/CrashDumpArch.cpp
    src/board/${MSOS_BOARD_DIR}/CrashDumpBoard.cpp
    src/core/Mutex.cpp
    src/core/Semaphore.cpp
    src/core/WaitQueue.cpp
    src/core/BlockPool.cpp
    src/core/Heap.cpp
    src/core/HeapOperators.cpp
    src/arch/${MSOS_ARCH_DIR}/ContextSwitch.s
    src/arch/${MSOS_ARCH_DIR}/Arch.cpp
    src/arch/${MSOS_ARCH_DIR}/FaultHandlers.s
    src/arch/${MSOS_ARCH_DIR}/Mpu.cpp
)

target_include_directories(kernel PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
)

target_include_directories(kernel PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
)

target_link_libraries(kernel PRIVATE startup)

target_compile_options(kernel PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti -fno-unwind-tables>
)

set_source_files_properties(
    src/arch/${MSOS_ARCH_DIR}/ContextSwitch.s
    src/arch/${MSOS_ARCH_DIR}/FaultHandlers.s
    PROPERTIES LANGUAGE ASM
)

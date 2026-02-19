add_library(kernel STATIC
    src/core/Thread.cpp
    src/core/Scheduler.cpp
    src/core/Kernel.cpp
    src/arch/cortex-m3/ContextSwitch.s
    src/arch/cortex-m3/CortexM.cpp
)

target_include_directories(kernel PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
)

target_compile_options(kernel PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti -fno-unwind-tables>
)

set_source_files_properties(src/arch/cortex-m3/ContextSwitch.s PROPERTIES
    LANGUAGE ASM
)

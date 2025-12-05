# Core Elysium library
add_library(elysium_core STATIC
    comms/dummy_comms.cpp
    sensors/dummy_sensor.cpp
)

target_include_directories(elysium_core
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)


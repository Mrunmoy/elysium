add_executable(sensor_hub
    main.cpp
)

target_link_libraries(sensor_hub
    PRIVATE
        elysium_core
)

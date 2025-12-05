#include <iostream>
#include <elysium/comms/dummy_comms.hpp>
#include <elysium/sensors/dummy_sensor.hpp>

int main()
{
    std::cout << "Elysium Sensor Hub Booted\n";
    elysium_dummy_comms();
    elysium_dummy_sensor();
    return 0;
}

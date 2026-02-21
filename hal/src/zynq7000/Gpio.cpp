// Zynq-7000 GPIO stub
//
// PYNQ-Z2 user LEDs are on PL (FPGA fabric) pins, not accessible without a
// bitstream. All GPIO functions are no-ops so apps that reference GPIO still
// link cleanly.

#include "hal/Gpio.h"

namespace hal
{
    void gpioInit(const GpioConfig & /* config */) {}
    void gpioSet(Port /* port */, std::uint8_t /* pin */) {}
    void gpioClear(Port /* port */, std::uint8_t /* pin */) {}
    void gpioToggle(Port /* port */, std::uint8_t /* pin */) {}

    bool gpioRead(Port /* port */, std::uint8_t /* pin */)
    {
        return false;
    }
}  // namespace hal

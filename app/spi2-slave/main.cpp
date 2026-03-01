// SPI1 Slave Echo Server -- Phase 14 board-to-board integration test
//
// Runs on Board 2 (CMSIS-DAP). Echoes each byte received on SPI1 back
// as the next response. Uses interrupt-driven RX. The SPI slave pre-loads
// the received byte into DR so the master reads it on the next clock cycle.
// LED on PC13 toggles on each echoed byte for visual activity indicator.
//
// Board: STM32F407ZGT6
//   SPI1: PA5 (SCK, AF5), PA6 (MISO, AF5), PA7 (MOSI, AF5)
//   LED: PC13 (active low)
//   Console: USART1 TX on PA9 (AF7)

#include "kernel/BoardConfig.h"
#include "hal/Gpio.h"
#include "hal/Rcc.h"
#include "hal/Spi.h"
#include "hal/Uart.h"

extern "C" const std::uint8_t g_boardDtb[];
extern "C" const std::uint32_t g_boardDtbSize;

namespace
{
    hal::UartId g_consoleUart;

    void print(const char *msg)
    {
        hal::uartWriteString(g_consoleUart, msg);
    }

    void initConsole()
    {
        const board::BoardConfig &cfg = board::config();

        if (cfg.hasConsoleTx)
        {
            hal::rccEnableGpioClock(hal::Port(cfg.consoleTx.port - 'A'));

            hal::GpioConfig txConfig{};
            txConfig.port = hal::Port(cfg.consoleTx.port - 'A');
            txConfig.pin = cfg.consoleTx.pin;
            txConfig.mode = hal::PinMode::AlternateFunction;
            txConfig.speed = hal::OutputSpeed::VeryHigh;
            txConfig.alternateFunction = cfg.consoleTx.af;
            hal::gpioInit(txConfig);
        }

        g_consoleUart = board::consoleUartId();
        hal::rccEnableUartClock(g_consoleUart);

        hal::UartConfig uartConfig{};
        uartConfig.id = g_consoleUart;
        uartConfig.baudRate = cfg.consoleBaud;
        hal::uartInit(uartConfig);
    }

    void initLed()
    {
        hal::rccEnableGpioClock(hal::Port::C);

        hal::GpioConfig ledConfig{};
        ledConfig.port = hal::Port::C;
        ledConfig.pin = 13;
        ledConfig.mode = hal::PinMode::Output;
        ledConfig.speed = hal::OutputSpeed::Low;
        hal::gpioInit(ledConfig);
    }

    // SPI slave RX callback: pre-load received byte as next TX, toggle LED.
    // Called in ISR context.
    void spiSlaveRxCallback(void * /* arg */, std::uint8_t rxByte)
    {
        hal::spiSlaveSetTxByte(hal::SpiId::Spi1, rxByte);
        hal::gpioToggle(hal::Port::C, 13);
    }

    void initSpi1Slave()
    {
        // Enable clocks
        hal::rccEnableGpioClock(hal::Port::A);
        hal::rccEnableSpiClock(hal::SpiId::Spi1);

        // PA5 = SPI1_SCK (AF5) -- AF push-pull, no pull, low speed
        hal::GpioConfig sckConfig{};
        sckConfig.port = hal::Port::A;
        sckConfig.pin = 5;
        sckConfig.mode = hal::PinMode::AlternateFunction;
        sckConfig.alternateFunction = 5;
        hal::gpioInit(sckConfig);

        // PA6 = SPI1_MISO (AF5) -- AF push-pull, no pull, low speed
        hal::GpioConfig misoConfig{};
        misoConfig.port = hal::Port::A;
        misoConfig.pin = 6;
        misoConfig.mode = hal::PinMode::AlternateFunction;
        misoConfig.alternateFunction = 5;
        hal::gpioInit(misoConfig);

        // PA7 = SPI1_MOSI (AF5) -- AF push-pull, no pull, low speed
        hal::GpioConfig mosiConfig{};
        mosiConfig.port = hal::Port::A;
        mosiConfig.pin = 7;
        mosiConfig.mode = hal::PinMode::AlternateFunction;
        mosiConfig.alternateFunction = 5;
        hal::gpioInit(mosiConfig);

        // Configure SPI1 as slave: mode 0, software NSS (SSI=0 = selected), 8-bit
        hal::SpiConfig spiConfig{};
        spiConfig.id = hal::SpiId::Spi1;
        spiConfig.mode = hal::SpiMode::Mode0;
        spiConfig.dataSize = hal::SpiDataSize::Bits8;
        spiConfig.bitOrder = hal::SpiBitOrder::MsbFirst;
        spiConfig.master = false;
        spiConfig.softwareNss = true;
        hal::spiInit(spiConfig);

        // Enable slave RX interrupt (also enables SPE -- slave peripheral is
        // now active).  SPE must be set before writing DR, because DR writes
        // may not reach the TX buffer while the peripheral is disabled.
        hal::spiSlaveRxInterruptEnable(hal::SpiId::Spi1, spiSlaveRxCallback, nullptr);

        // Pre-load 0x00 as initial response (SPE is now enabled)
        hal::spiSlaveSetTxByte(hal::SpiId::Spi1, 0x00);
    }
}  // namespace

int main()
{
    board::configInit(g_boardDtb, g_boardDtbSize);
    initConsole();
    initLed();

    print("\r\n=== SPI1 Slave Echo Server (Phase 14) ===\r\n");
    print("Board 2: SPI1 slave echo, LED on PC13\r\n");
    print("Pins: PA5(SCK) PA6(MISO) PA7(MOSI)\r\n\r\n");

    initSpi1Slave();

    print("SPI1 Slave Echo Server ready\r\n");

    // LED OFF at start (active low: set = OFF)
    hal::gpioSet(hal::Port::C, 13);

    while (true)
    {
        // Aggressively clear all debug state that OpenOCD may have left.
        // This runs every loop iteration so it catches late debug connections.

        // 1. Clear C_DEBUGEN via DHCSR (DBGKEY=0xA05F, C_DEBUGEN=0)
        *reinterpret_cast<volatile std::uint32_t *>(0xE000EDF0) = 0xA05F0000;

        // 2. Clear DEMCR: disable TRCENA and MON_EN
        *reinterpret_cast<volatile std::uint32_t *>(0xE000EDFC) &=
            ~((1U << 24) | (1U << 16));

        // 3. Disable FPB and clear all comparators
        *reinterpret_cast<volatile std::uint32_t *>(0xE0002000) = (1U << 1);
        for (std::uint32_t i = 0; i < 8; ++i)
        {
            *reinterpret_cast<volatile std::uint32_t *>(0xE0002008 + i * 4) = 0;
        }

        // 4. Disable all DWT comparators
        for (std::uint32_t i = 0; i < 4; ++i)
        {
            *reinterpret_cast<volatile std::uint32_t *>(0xE0001028 + i * 0x10) = 0;
        }

        __asm volatile("wfi");
    }
}

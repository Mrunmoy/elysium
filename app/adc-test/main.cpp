// ADC on-target validation app (STM32F407).
//
// Uses internal channels to avoid external wiring:
// - Channel 17: VREFINT
// - Channel 16: temperature sensor

#include "kernel/BoardConfig.h"
#include "hal/Adc.h"
#include "hal/Gpio.h"
#include "hal/Rcc.h"
#include "hal/Uart.h"
#include "msos/ErrorCode.h"

#include <cstdint>

extern "C" const std::uint8_t g_boardDtb[];
extern "C" const std::uint32_t g_boardDtbSize;
extern "C"
{
    volatile std::uint32_t g_adcTestResult = 0;
}

namespace
{
    hal::UartId g_consoleUart;
    constexpr std::uint32_t kReadTimeoutLoops = 2000000;

    void print(const char *msg)
    {
        hal::uartWriteString(g_consoleUart, msg);
    }

    void printDecimal(std::uint32_t value)
    {
        if (value == 0)
        {
            hal::uartPutChar(g_consoleUart, '0');
            return;
        }

        char buf[10];
        int pos = 0;
        while (value > 0 && pos < 10)
        {
            buf[pos++] = '0' + static_cast<char>(value % 10);
            value /= 10;
        }
        for (int i = pos - 1; i >= 0; --i)
        {
            hal::uartPutChar(g_consoleUart, buf[i]);
        }
    }

    void printResult(const char *name, bool pass)
    {
        print(name);
        print(pass ? ": PASS\r\n" : ": FAIL\r\n");
    }

    void printMachineCase(const char *caseName, bool pass)
    {
        print("MSOS_CASE:adc:");
        print(caseName);
        print(pass ? ":PASS\r\n" : ":FAIL\r\n");
    }

    void printMachineSummary(std::uint32_t passCount, std::uint32_t totalCount)
    {
        print("MSOS_SUMMARY:adc:pass=");
        printDecimal(passCount);
        print(":total=");
        printDecimal(totalCount);
        print(":result=");
        print(passCount == totalCount ? "PASS\r\n" : "FAIL\r\n");
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

    void delayMs(std::uint32_t ms)
    {
        for (std::uint32_t i = 0; i < ms; ++i)
        {
            for (volatile std::uint32_t j = 0; j < 33600; ++j)
            {
            }
        }
    }

    bool readStableChannel(std::uint8_t channel, std::uint16_t *first, std::uint16_t *second)
    {
        std::uint16_t a = 0;
        std::uint16_t b = 0;
        std::int32_t s1 = hal::adcRead(hal::AdcId::Adc1, channel, &a, kReadTimeoutLoops);
        std::int32_t s2 = hal::adcRead(hal::AdcId::Adc1, channel, &b, kReadTimeoutLoops);
        if (s1 != msos::error::kOk || s2 != msos::error::kOk)
        {
            return false;
        }
        *first = a;
        *second = b;
        return true;
    }

    bool testVrefintRead()
    {
        std::uint16_t a = 0;
        std::uint16_t b = 0;
        if (!readStableChannel(17, &a, &b))
        {
            print("  [vref read timeout]\r\n");
            return false;
        }

        // Simple sanity + repeatability check to avoid over-constraining board variance.
        if (a == 0 || b == 0 || a > 4095 || b > 4095)
        {
            print("  [vref out of range]\r\n");
            return false;
        }

        std::uint16_t diff = (a > b) ? (a - b) : (b - a);
        if (diff > 200)
        {
            print("  [vref unstable]\r\n");
            return false;
        }
        return true;
    }

    bool testTempSensorRead()
    {
        std::uint16_t a = 0;
        std::uint16_t b = 0;
        if (!readStableChannel(16, &a, &b))
        {
            print("  [temp read timeout]\r\n");
            return false;
        }

        if (a == 0 || b == 0 || a > 4095 || b > 4095)
        {
            print("  [temp out of range]\r\n");
            return false;
        }

        std::uint16_t diff = (a > b) ? (a - b) : (b - a);
        if (diff > 300)
        {
            print("  [temp unstable]\r\n");
            return false;
        }
        return true;
    }

    bool testInvalidChannelRejected()
    {
        std::uint16_t value = 0;
        return hal::adcRead(hal::AdcId::Adc1, 19, &value, kReadTimeoutLoops) == msos::error::kInvalid;
    }
}  // namespace

int main()
{
    board::configInit(g_boardDtb, g_boardDtbSize);
    initConsole();
    g_adcTestResult = 0xADC00001U;  // started

    // Wait for USB-UART host to start capture.
    delayMs(500);

    hal::AdcConfig cfg{};
    cfg.id = hal::AdcId::Adc1;
    cfg.resolution = hal::AdcResolution::Bits12;
    cfg.align = hal::AdcAlign::Right;
    cfg.sampleTime = hal::AdcSampleTime::Cycles480;
    hal::adcInit(cfg);

    print("\r\n=== ADC Internal Channel Test (Phase 20) ===\r\n");

    std::uint32_t total = 3;
    std::uint32_t pass = 0;

    bool ok = testVrefintRead();
    printResult("vrefint-read", ok);
    printMachineCase("vrefint-read", ok);
    if (ok)
    {
        ++pass;
    }

    ok = testTempSensorRead();
    printResult("temp-read", ok);
    printMachineCase("temp-read", ok);
    if (ok)
    {
        ++pass;
    }

    ok = testInvalidChannelRejected();
    printResult("invalid-channel", ok);
    printMachineCase("invalid-channel", ok);
    if (ok)
    {
        ++pass;
    }

    printMachineSummary(pass, total);
    print("=== ADC Test Complete ===\r\n");
    g_adcTestResult = 0xADC10000U |
                      ((pass & 0xFFU) << 8) |
                      (total & 0xFFU);

    while (true)
    {
    }
}

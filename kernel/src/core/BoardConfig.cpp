// Board configuration: populated from DTB at runtime.

#include "kernel/BoardConfig.h"
#include "kernel/Fdt.h"
#include "hal/Uart.h"

#include <cstring>

namespace board
{
namespace
{
    BoardConfig s_config;

    void readPinConfig(const std::uint8_t *dtb, std::int32_t nodeOff,
                       PinConfig &pin)
    {
        const char *portStr = nullptr;
        if (kernel::fdt::readString(dtb, nodeOff, "port", portStr) &&
            portStr != nullptr && portStr[0] != '\0')
        {
            pin.port = portStr[0];
        }
        else
        {
            pin.port = 0;
        }

        std::uint32_t val = 0;
        if (kernel::fdt::readU32(dtb, nodeOff, "pin", val))
        {
            pin.pin = static_cast<std::uint8_t>(val);
        }
        if (kernel::fdt::readU32(dtb, nodeOff, "af", val))
        {
            pin.af = static_cast<std::uint8_t>(val);
        }
    }

}  // namespace

void configInit(const std::uint8_t *dtb, std::uint32_t dtbSize)
{
    std::memset(&s_config, 0, sizeof(s_config));

    if (!kernel::fdt::validate(dtb, dtbSize))
    {
        return;
    }

    // Board identity
    std::int32_t boardNode = kernel::fdt::findNode(dtb, "/board");
    if (boardNode >= 0)
    {
        kernel::fdt::readString(dtb, boardNode, "name", s_config.boardName);
        kernel::fdt::readString(dtb, boardNode, "mcu", s_config.mcu);
        kernel::fdt::readString(dtb, boardNode, "arch", s_config.arch);
    }

    // Clocks
    std::int32_t clocksNode = kernel::fdt::findNode(dtb, "/clocks");
    if (clocksNode >= 0)
    {
        kernel::fdt::readU32(dtb, clocksNode, "system-clock", s_config.systemClock);
        kernel::fdt::readU32(dtb, clocksNode, "apb1-clock", s_config.apb1Clock);
        kernel::fdt::readU32(dtb, clocksNode, "apb2-clock", s_config.apb2Clock);
        kernel::fdt::readU32(dtb, clocksNode, "hse-clock", s_config.hseClock);
    }

    // Memory regions
    std::int32_t memNode = kernel::fdt::findNode(dtb, "/memory");
    if (memNode >= 0)
    {
        std::int32_t child = kernel::fdt::firstChild(dtb, memNode);
        while (child >= 0 && s_config.memoryRegionCount < kMaxMemoryRegions)
        {
            MemoryRegion &region = s_config.memoryRegions[s_config.memoryRegionCount];
            region.name = kernel::fdt::nodeName(dtb, child);
            if (kernel::fdt::readReg(dtb, child, region.base, region.size))
            {
                ++s_config.memoryRegionCount;
            }
            child = kernel::fdt::nextSibling(dtb, child);
        }
    }

    // Console
    std::int32_t consoleNode = kernel::fdt::findNode(dtb, "/console");
    if (consoleNode >= 0)
    {
        kernel::fdt::readString(dtb, consoleNode, "uart", s_config.consoleUart);
        kernel::fdt::readU32(dtb, consoleNode, "baud", s_config.consoleBaud);

        std::int32_t txNode = kernel::fdt::findNode(dtb, "/console/tx");
        if (txNode >= 0)
        {
            s_config.hasConsoleTx = true;
            readPinConfig(dtb, txNode, s_config.consoleTx);
        }

        std::int32_t rxNode = kernel::fdt::findNode(dtb, "/console/rx");
        if (rxNode >= 0)
        {
            s_config.hasConsoleRx = true;
            readPinConfig(dtb, rxNode, s_config.consoleRx);
        }
    }

    // LED
    std::int32_t ledNode = kernel::fdt::findNode(dtb, "/led");
    if (ledNode >= 0)
    {
        const char *portStr = nullptr;
        if (kernel::fdt::readString(dtb, ledNode, "port", portStr))
        {
            s_config.hasLed = true;
            readPinConfig(dtb, ledNode, s_config.led);
        }
    }

    // Features
    std::int32_t featNode = kernel::fdt::findNode(dtb, "/features");
    if (featNode >= 0)
    {
        s_config.hasFpu = kernel::fdt::hasProperty(dtb, featNode, "fpu");
    }
}

const BoardConfig &config()
{
    return s_config;
}

hal::UartId consoleUartId()
{
    if (s_config.consoleUart == nullptr)
    {
        return hal::UartId::Usart1;
    }

    if (std::strcmp(s_config.consoleUart, "usart1") == 0)
    {
        return hal::UartId::Usart1;
    }
    if (std::strcmp(s_config.consoleUart, "usart2") == 0)
    {
        return hal::UartId::Usart2;
    }
    if (std::strcmp(s_config.consoleUart, "usart3") == 0)
    {
        return hal::UartId::Usart3;
    }
    if (std::strcmp(s_config.consoleUart, "uart0") == 0)
    {
        return hal::UartId::Uart0;
    }
    if (std::strcmp(s_config.consoleUart, "uart4") == 0)
    {
        return hal::UartId::Uart4;
    }
    if (std::strcmp(s_config.consoleUart, "uart5") == 0)
    {
        return hal::UartId::Uart5;
    }
    if (std::strcmp(s_config.consoleUart, "usart6") == 0)
    {
        return hal::UartId::Usart6;
    }

    return hal::UartId::Usart1;
}

void configReset()
{
    std::memset(&s_config, 0, sizeof(s_config));
}

}  // namespace board

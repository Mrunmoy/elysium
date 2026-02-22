// Kernel shell implementation.
//
// Provides an interactive CLI with built-in commands for inspecting
// kernel state.  All I/O goes through function pointers so the shell
// is hardware-independent and testable on the host.

#include "kernel/Shell.h"
#include "kernel/Thread.h"
#include "kernel/Kernel.h"
#include "kernel/Heap.h"
#include "kernel/Ipc.h"

#include <cstdint>
#include <cstring>

namespace kernel
{
namespace
{
    // Shell state
    ShellWriteFn s_writeFn = nullptr;
    static constexpr std::uint8_t kMaxLineLength = 80;
    char s_lineBuffer[kMaxLineLength + 1];
    std::uint8_t s_linePos = 0;

    static constexpr const char *kVersion = "ms-os v0.9.0";

    // Integer-to-string (no sprintf in freestanding environment)
    void uintToStr(std::uint32_t val, char *buf, std::size_t bufSize)
    {
        if (bufSize == 0)
        {
            return;
        }
        if (val == 0)
        {
            if (bufSize >= 2)
            {
                buf[0] = '0';
                buf[1] = '\0';
            }
            return;
        }
        char tmp[11];
        std::size_t len = 0;
        while (val > 0 && len < sizeof(tmp))
        {
            tmp[len++] = '0' + static_cast<char>(val % 10);
            val /= 10;
        }
        std::size_t i = 0;
        while (len > 0 && i < bufSize - 1)
        {
            buf[i++] = tmp[--len];
        }
        buf[i] = '\0';
    }

    void write(const char *str)
    {
        if (s_writeFn != nullptr)
        {
            s_writeFn(str);
        }
    }

    void writeLine(const char *str)
    {
        write(str);
        write("\r\n");
    }

    // ---- Built-in commands ----

    void cmdHelp()
    {
        writeLine("commands:");
        writeLine("  help    - show this message");
        writeLine("  ps      - list threads");
        writeLine("  mem     - heap statistics");
        writeLine("  uptime  - ticks since boot");
        writeLine("  version - show version");
    }

    const char *threadStateName(ThreadState state)
    {
        switch (state)
        {
            case ThreadState::Ready:   return "Ready ";
            case ThreadState::Running: return "Run   ";
            case ThreadState::Blocked: return "Block ";
            default:                   return "???   ";
        }
    }

    void writeField(const char *text, std::size_t width)
    {
        write(text);
        std::size_t len = std::strlen(text);
        while (len < width)
        {
            write(" ");
            ++len;
        }
    }

    void cmdPs()
    {
        writeLine("TID  NAME         STATE   PRI  STACK");
        writeLine("---  ----------   ------  ---  -----");

        ThreadControlBlock *tcbs = threadGetTcbArray();
        char buf[16];

        for (ThreadId i = 0; i < kMaxThreads; ++i)
        {
            ThreadControlBlock &tcb = tcbs[i];
            if (tcb.state == ThreadState::Inactive)
            {
                continue;
            }

            // TID
            uintToStr(tcb.id, buf, sizeof(buf));
            writeField(buf, 5);

            // Name
            const char *name = tcb.name != nullptr ? tcb.name : "(null)";
            writeField(name, 13);

            // State
            write(threadStateName(tcb.state));
            write("  ");

            // Priority
            uintToStr(tcb.currentPriority, buf, sizeof(buf));
            writeField(buf, 5);

            // Stack size
            uintToStr(tcb.stackSize, buf, sizeof(buf));
            writeLine(buf);
        }
    }

    void cmdMem()
    {
        HeapStats stats = heapGetStats();
        char buf[16];

        write("total:    ");
        uintToStr(stats.totalSize, buf, sizeof(buf));
        writeLine(buf);

        write("used:     ");
        uintToStr(stats.usedSize, buf, sizeof(buf));
        writeLine(buf);

        write("free:     ");
        uintToStr(stats.freeSize, buf, sizeof(buf));
        writeLine(buf);

        write("peak:     ");
        uintToStr(stats.highWatermark, buf, sizeof(buf));
        writeLine(buf);

        write("allocs:   ");
        uintToStr(stats.allocCount, buf, sizeof(buf));
        writeLine(buf);

        write("largest:  ");
        uintToStr(stats.largestFreeBlock, buf, sizeof(buf));
        writeLine(buf);
    }

    void cmdUptime()
    {
        std::uint32_t ticks = tickCount();
        char buf[16];

        uintToStr(ticks, buf, sizeof(buf));
        write(buf);
        write(" ticks (");

        std::uint32_t seconds = ticks / 1000;
        uintToStr(seconds, buf, sizeof(buf));
        write(buf);
        writeLine("s)");
    }

    void cmdVersion()
    {
        writeLine(kVersion);
    }

    // Command dispatch table
    struct Command
    {
        const char *name;
        void (*handler)();
    };

    static constexpr Command kCommands[] =
    {
        {"help",    cmdHelp},
        {"ps",      cmdPs},
        {"mem",     cmdMem},
        {"uptime",  cmdUptime},
        {"version", cmdVersion},
    };

    static constexpr std::uint8_t kCommandCount =
        sizeof(kCommands) / sizeof(kCommands[0]);

    void executeCommand(const char *line)
    {
        // Skip leading whitespace
        while (*line == ' ')
        {
            ++line;
        }

        // Empty line
        if (*line == '\0')
        {
            return;
        }

        for (std::uint8_t i = 0; i < kCommandCount; ++i)
        {
            if (std::strcmp(line, kCommands[i].name) == 0)
            {
                kCommands[i].handler();
                return;
            }
        }

        write("unknown command: ");
        writeLine(line);
    }

}  // namespace

    void shellInit(const ShellConfig &config)
    {
        s_writeFn = config.writeFn;
        s_linePos = 0;
        s_lineBuffer[0] = '\0';
    }

    void shellProcessChar(char c)
    {
        // Enter (CR or LF)
        if (c == '\r' || c == '\n')
        {
            write("\r\n");
            s_lineBuffer[s_linePos] = '\0';
            executeCommand(s_lineBuffer);
            s_linePos = 0;
            shellPrompt();
            return;
        }

        // Backspace (0x08) or DEL (0x7F)
        if (c == '\b' || c == 0x7F)
        {
            if (s_linePos > 0)
            {
                --s_linePos;
                write("\b \b");
            }
            return;
        }

        // Printable characters
        if (c >= ' ' && c < 0x7F)
        {
            if (s_linePos < kMaxLineLength)
            {
                s_lineBuffer[s_linePos++] = c;
                char echo[2] = {c, '\0'};
                write(echo);
            }
        }
    }

    void shellPrompt()
    {
        write("ms-os> ");
    }

    void shellReset()
    {
        s_writeFn = nullptr;
        s_linePos = 0;
        s_lineBuffer[0] = '\0';
    }

}  // namespace kernel

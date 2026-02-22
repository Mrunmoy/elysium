# Phase 8: Power Management

## Overview

Adds CPU sleep and peripheral clock gating to reduce power consumption
when threads are idle. The idle thread now executes WFI (Wait For Interrupt)
instead of spinning, and the kernel exposes sleep mode control for
application-level power policies.

## Architecture API (kernel/inc/kernel/Arch.h)

```cpp
namespace kernel::arch
{
    // Put CPU to sleep until any enabled interrupt fires.
    // Idle thread calls this in its main loop.
    void waitForInterrupt();

    // When enabled, CPU enters WFI automatically on ISR-to-thread return.
    // Cortex-M: sets SLEEPONEXIT bit in SCB->SCR.
    // Cortex-A9: no-op (no hardware equivalent).
    void enableSleepOnExit();
    void disableSleepOnExit();

    // When enabled, WFI enters deep sleep (Stop mode on STM32).
    // Cortex-M: sets SLEEPDEEP bit in SCB->SCR.
    // Cortex-A9: no-op.
    void enableDeepSleep();
    void disableDeepSleep();
}
```

## Implementation

### Cortex-M3/M4

All functions manipulate the System Control Register (SCB->SCR at 0xE000ED10):

| Bit | Name | Effect |
|-----|------|--------|
| 1 | SLEEPONEXIT | Auto-WFI on exception return |
| 2 | SLEEPDEEP | WFI enters Stop mode (PLLs off) |

`waitForInterrupt()` executes the `wfi` instruction with a memory barrier.

### Cortex-A9

`waitForInterrupt()` executes `wfi`. The SCR functions are no-ops because
the A9 does not have SLEEPONEXIT or SLEEPDEEP hardware.

### Idle Thread

```cpp
static void idleThreadFunc(void *)
{
    while (true)
    {
        arch::waitForInterrupt();
    }
}
```

The idle thread runs at priority 31 (lowest). When no other thread is
ready, the scheduler falls back to idle, which immediately puts the CPU
to sleep. Any interrupt (SysTick, UART, GPIO) wakes it.

## Peripheral Clock Gating (HAL)

```cpp
namespace hal
{
    void rccDisableGpioClock(Port port);
    void rccDisableUartClock(UartId id);
}
```

STM32: clears the enable bit in AHB1ENR/APB1ENR/APB2ENR.
Zynq: writes APER_CLK_CTRL via SLCR (with unlock/lock sequence).

## Test Coverage

10 tests in `test/kernel/PowerTest.cpp`:
- WFI counter increments
- Sleep-on-exit enable/disable
- Deep sleep enable/disable
- Independence of sleep-on-exit and deep sleep
- Reset clears all power state

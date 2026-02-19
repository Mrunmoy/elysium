# ms-os Project Memory

## Current Status (2026-02-20)
- Phase 0 (Foundation) COMPLETE
- Phase 1 (Kernel Core) COMPLETE and VERIFIED on hardware (commit af65fad)
- Crash dump system COMPLETE (commit bc73012)
- Cross-compilation: threads demo 5836 B FLASH, 23 KB SRAM; blinky 1644 B FLASH
- Host tests: 33/33 C++ passing (7 GPIO + 11 Thread + 15 Scheduler)
- Python tests: 17/17 passing (crash monitor tool)
- LED toggling verified via J-Link register reads (ODR changes at ~500ms)
- Context switching verified: g_currentTcb and g_nextTcb point to different TCBs
- Crash dump: SHCSR shows all 3 fault handlers enabled, CCR has DIV_0_TRP + UNALIGN_TRP
- UART serial RX not working (CP2102 wiring issue -- PA9 to RX needs checking)
- GitHub repo: git@github.com:Mrunmoy/elysium.git
- Branches: main, phase-0, phase-1/kernel-core (all pushed)

## Next Steps
- Fix UART serial wiring (PA9 to CP2102 RX) -- needed to verify crash dump UART output
- Phase 2: Priority scheduling, mutexes, semaphores
- See CLAUDE.md for full project vision

## Build Commands
- `nd` -- alias for `nix develop` (added to ~/.bashrc)
- `python3 build.py` -- cross-compile firmware
- `python3 build.py -t` -- host unit tests
- `python3 build.py -c` -- clean
- `python3 build.py -f` -- flash threads via JLinkExe
- `python3 build.py -f --app blinky` -- flash blinky
- Host tests must run outside Nix (glibc mismatch): `cd build-test && cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug .. && ninja && ctest`

## Key Architecture Decisions
- Register-level HAL (direct register access, not wrapping ST HAL library)
- Dual CMake build: CMAKE_CROSSCOMPILING selects cross vs host targets
- Link-time mock substitution for host tests (MockGpio.cpp replaces Gpio.cpp)
- SystemInit.cpp uses extern "C" for assembly linkage
- STM32F207ZGT6: 120 MHz via PLL (HSE 25MHz, M=25, N=240, P=2, Q=5)
- Flash: 3 wait states; APB1 /4 = 30 MHz, APB2 /2 = 60 MHz
- Cortex-M3 (no FPU, no CCM memory)
- C++17 (not C++20) -- designated initializers not available, use explicit field assignment
- LED on PC13, USART1 TX on PA9 (AF7)

## Vendor Submodule Versions
- stm32f4xx-hal-driver: v1.8.5
- cmsis-device-f2: v2.2.6
- cmsis_core (CMSIS_5): 5.9.0
- googletest: v1.14.0

## Environment
- Nix installed (single-user, /nix owned by mrumoy)
- ~/.config/nix/nix.conf has `experimental-features = nix-command flakes`
- ~/.bashrc sources nix profile + has `alias nd='nix develop'`
- GCC 13.2.0 (host), ARM GCC 12.3.1 (via Nix)

## User Preferences
- No Co-Authored-By line in git commit messages

## Gotchas Encountered
- Designated initializers (.field = val) are C++20, not C++17 -- use explicit assignment
- `-nostartfiles` requires `_init`/`_fini` stubs (added to Startup.s)
- Nix flakes require files to be git-tracked (`git add flake.nix` before `nix develop`)
- `/nix` directory needs root to create (`sudo mkdir -m 0755 /nix && sudo chown mrumoy /nix`)
- F207 has no FPU (Cortex-M3) -- remove FPU enable from Reset_Handler and FPU_IRQHandler from vector table
- F207 has no CCM memory region -- remove from linker script
- F207 does not need PWR voltage scaling (no VOS register config needed)
- Board has USART1 on PA9/PA10 (not USART2 on PA2) -- CP2102 USB-serial on /dev/ttyUSB0
- J-Link must use SWD interface (JTAG auto-detect fails); -if SWD works
- Static library weak symbol override: linker won't pull .o from .a if weak symbol already resolves the reference. Fix: -Wl,--whole-archive for the library
- yield() must call switchContext() + update g_nextTcb, not just pend PendSV -- otherwise PendSV saves/restores the same thread
- `~0x7u` on 64-bit host: unsigned int is 32-bit, so `~0x7u` = 0xFFFFFFF8, masking upper 32 bits of pointer! Use `~std::uintptr_t{7}` instead
- `static alignas(8)` ordering: ARM GCC 12 requires `alignas(8) static` (alignas before static)
- Pointer casts for stack frame: on x86_64, cast through `std::uintptr_t` then truncate to `uint32_t`
- 17-word stack frame causes 4-byte misalignment; use 16-word frame (no EXC_RETURN on stack) for natural 8-byte alignment

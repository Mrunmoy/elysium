# PYNQ-Z2 Bare-Metal Bringup Notes

## Hardware Setup

- **Board:** PYNQ-Z2 (TUL), Zynq-7020 (XC7Z020-1CLG400C)
- **SoC:** Dual Cortex-A9 MPCore @ 650 MHz, 512 MB DDR3
- **Connection:** Single micro-USB cable (PROG/UART port)
- **FTDI FT2232HQ** on-board provides two channels:
  - Channel A: JTAG (`/dev/ttyUSB1` disappears when OpenOCD claims it)
  - Channel B: UART console (`/dev/ttyUSB2`, 115200 baud)
- **Boot mode:** SD card (default jumper setting)
- **SD card:** Ships with Linux (PYNQ image); u-boot initializes PS before Linux

### USB Device Mapping

| Device       | Function          | Notes                              |
|--------------|-------------------|------------------------------------|
| /dev/ttyUSB0 | J-Link (F407)     | Remains connected, no conflict     |
| /dev/ttyUSB1 | FTDI Ch.A (JTAG)  | Disappears when OpenOCD runs       |
| /dev/ttyUSB2 | FTDI Ch.B (UART)  | 115200 baud, use pyserial to read  |

## Development Approach

The PYNQ boots Linux from SD card. We use OpenOCD + JTAG to halt the CPUs,
load our bare-metal binary into DDR, and run it -- no SD card swapping needed.
u-boot has already initialized the PS (DDR controller, PLLs, MIO pin muxing,
UART, clocks), so our SystemInit only needs to read clock frequencies, not
configure them.

## OpenOCD Configuration

File: `openocd/pynq-z2.cfg`

```
adapter driver ftdi
ftdi vid_pid 0x0403 0x6010
ftdi channel 0
ftdi layout_init 0x0088 0x008b
adapter speed 10000
source [find target/zynq_7000.cfg]
```

### OpenOCD udev Rule

Required for non-root access. Copy from:
```
/nix/store/.../openocd-0.12.0/share/openocd/contrib/60-openocd.rules
```
to `/etc/udev/rules.d/60-openocd.rules`, then `sudo udevadm control --reload-rules`.

### FTDI VID/PID

The on-board FTDI identifies as Xilinx (re-branded):
```
usb-Xilinx_TUL_1234-tul-if00-port0  (JTAG)
usb-Xilinx_TUL_1234-tul-if01-port0  (UART)
```
VID:PID = 0403:6010 (standard FTDI FT2232).

## Zynq-7020 Key Addresses

Source: Zynq TRM (UG585) + hardware debugging sessions.

| Resource              | Address       | Notes                                |
|-----------------------|---------------|--------------------------------------|
| DDR                   | 0x00000000    | 512 MB; usable from 0x00100000       |
| OCM                   | 0xFFFF0000    | 64 KB on-chip memory                 |
| PS UART0              | 0xE0000000    | Cadence UART, connected to FTDI      |
| PS UART1              | 0xE0001000    | Second UART                          |
| PS GPIO               | 0xE000A000    | 54 MIO + 64 EMIO pins               |
| SLCR                  | 0xF8000000    | System Level Control Registers       |
| SLCR Unlock           | 0xF8000008    | Write 0xDF0D to unlock               |
| APER_CLK_CTRL         | 0xF800012C    | AMBA peripheral clock gating         |
| SCU Periph Base       | 0xF8F00000    | MPCore private peripherals           |
| GIC CPU Interface     | 0xF8F00100    | ICCICR, ICCIAR, ICCEOIR              |
| Private Timer         | 0xF8F00600    | Per-CPU tick timer                   |
| GIC Distributor       | 0xF8F01000    | ICDDCR, interrupt config             |
| L2 Cache (PL310)      | 0xF8F02000    | 512 KB L2 cache controller           |
| CPU0 Debug Unit       | 0x80090000    | Via APB debug port (DAP AP#1)        |
| CPU1 Debug Unit       | 0x80092000    | Via APB debug port (DAP AP#1)        |

### APER_CLK_CTRL Bit Map (Relevant Bits)

| Bit | Peripheral |
|-----|------------|
| 0   | DMA        |
| 2   | USB1       |
| 3   | USB0       |
| 10  | SPI1       |
| 20  | UART0      |
| 21  | UART1      |
| 22  | GPIO       |
| 23  | QSPI       |
| 24  | SMC        |

Observed value after Linux boot: `0x01D0044D` (UART0, GPIO, QSPI, SMC, SPI1,
USB0, USB1, DMA all enabled by u-boot).

### UART Control Register (CR) Bit Map

Per Zynq TRM Table 19-1 (address: UART_base + 0x00):

| Bit | Name     | Description            |
|-----|----------|------------------------|
| 0   | RXRST    | RX logic reset         |
| 1   | TXRST    | TX logic reset         |
| 2   | RXEN     | RX enable              |
| 3   | RXDIS    | RX disable             |
| 4   | TXEN     | TX enable              |
| 5   | TXDIS    | TX disable             |
| 6   | RSTTO    | Restart timeout counter|
| 7   | STTBRK   | Start break (TXD low!) |
| 8   | STPBRK   | Stop break             |

**Critical:** Bits 7-8 are STARTBRK/STOPBRK. Writing bit 7 forces the TXD
line LOW, making the UART appear dead. This was the root cause of our UART
silence bug (see Bugs Found below).

## JTAG Load Procedure

The correct sequence for loading bare-metal code while Linux is on the SD:

```
1. Start OpenOCD
2. Halt BOTH CPUs (cpu0 and cpu1)
3. Disable MMU on BOTH CPUs (critical -- cpu1's MMU causes data aborts)
4. Invalidate TLBs and I-cache on both
5. Disable GIC distributor (prevents pending IRQs from firing)
6. Clear all pending GIC interrupts
7. Set VBAR to our vector table address (0x00100000)
8. Load ELF image
9. Set CPSR with IRQ+FIQ disabled (0x000000D3 = SVC mode, I+F bits set)
10. Resume at _boot entry point
```

### OpenOCD Commands (Telnet on port 4444)

```tcl
# Halt both CPUs
targets zynq.cpu0
halt
targets zynq.cpu1
halt

# Disable MMU on CPU0
targets zynq.cpu0
arm mcr 15 0 1 0 0 0x00C50878    ;# SCTLR: MMU/cache/BP off
arm mcr 15 0 8 7 0 0              ;# TLBIALL
arm mcr 15 0 7 5 0 0              ;# ICIALLU

# Disable MMU on CPU1
targets zynq.cpu1
arm mcr 15 0 1 0 0 0x00C50878
arm mcr 15 0 8 7 0 0
arm mcr 15 0 7 5 0 0

# Disable GIC (prevents pending Linux IRQs from preempting our code)
targets zynq.cpu0
mww 0xF8F01000 0x00000000         ;# GIC Distributor disable
mww 0xF8F00100 0x00000000         ;# GIC CPU Interface disable
mww 0xF8F01280 0xFFFFFFFF         ;# Clear pending IRQs (ICDICPR0)
mww 0xF8F01284 0xFFFFFFFF         ;# Clear pending IRQs (ICDICPR1)
mww 0xF8F01288 0xFFFFFFFF         ;# Clear pending IRQs (ICDICPR2)

# Set VBAR and load
arm mcr 15 0 12 0 0 0x00100000    ;# VBAR = vector table
load_image build/app/hello/hello

# Set CPSR (SVC mode, IRQ+FIQ disabled) and resume
reg cpsr 0x000000D3
resume 0x00100020                  ;# _boot entry (after vector table)
```

## Bugs Found and Fixed

### Bug 1: UART CR Register Bit Definitions (Critical)

**File:** `hal/src/zynq7000/Uart.cpp`
**Commit:** 7ae11e4

The UART Control Register bit definitions were wrong:

| Constant   | Old (Wrong) | New (Correct) | Effect of Old Value        |
|------------|-------------|---------------|----------------------------|
| kCrRxRes   | 1 << 8      | 1 << 0        | Was writing STOPBRK        |
| kCrTxRes   | 1 << 7      | 1 << 1        | Was writing STARTBRK (!!!) |
| kCrRxEn    | 1 << 6      | 1 << 2        | Was writing RSTTO          |
| kCrRxDis   | 1 << 5      | 1 << 3        | Was writing TXDIS          |
| kCrTxEn    | 1 << 4      | 1 << 4        | Correct (lucky)            |
| kCrTxDis   | 1 << 3      | 1 << 5        | Was writing RXDIS          |

The init sequence wrote `kCrTxRes | kCrRxRes` = `(1<<7) | (1<<8)` = 0x180,
which asserted **STARTBRK** (bit 7). STARTBRK forces TXD LOW permanently,
making the UART appear completely dead. No amount of subsequent writes to
the FIFO would produce output.

The `kernel/src/board/pynq-z2/CrashDumpBoard.cpp` file had the correct bit
definitions (it was written later with better reference material), which made
the inconsistency harder to spot.

### Bug 2: APER_CLK_CTRL Bit Positions (Masked by u-boot)

**File:** `hal/src/zynq7000/Rcc.cpp`
**Commit:** 7ae11e4

| Constant      | Old (Wrong) | New (Correct) | Effect of Old Value         |
|---------------|-------------|---------------|-----------------------------|
| kUart0ClkBit  | 0           | 20            | Was toggling DMA clock bit  |
| kUart1ClkBit  | 1           | 21            | Was toggling USB0 clock bit |

This bug was masked in practice because u-boot already enables UART0's clock
(bit 20) before we run. Our code was writing bit 0 (DMA), which was also
already set. The bug would only manifest if running without u-boot (e.g.,
JTAG boot mode with ps7_init).

## Debugging Findings

### Problem: Pending IRQs Pre-empt Boot Code

When Linux is running, the GIC has active timer interrupts (private timer,
watchdog, etc.). After halting the CPUs and loading our binary:

1. `resume 0x00100000` starts executing at the Reset vector
2. An IRQ fires IMMEDIATELY -- before `cpsid if` can execute
3. The CPU vectors to IRQ_Handler (which is a weak infinite loop in hello app)
4. The CPU is stuck forever in the default handler

**Evidence:** CPU halted in IRQ mode at PC=0x00100100 (the weak IRQ_Handler),
LR_irq=0x00100004 (= Reset vector + 4, meaning the IRQ hit at the very first
instruction).

**Solution:** Disable the GIC and clear pending interrupts via OpenOCD `mww`
commands BEFORE resume (see JTAG Load Procedure above). Also set CPSR with
I+F bits to disable IRQs.

### Problem: CPU1 MMU Causes Data Aborts

After halting and disabling MMU on CPU0 only, OpenOCD memory reads (`mdw`)
of peripheral addresses (0xE0000000, 0xF800012C) return data aborts with
DFSR=0x00000005 (translation fault).

**Cause:** OpenOCD routes some memory accesses through CPU1, which still has
Linux's MMU enabled. Virtual address 0xE0000000 doesn't map to physical
0xE0000000 in Linux's page tables.

**Solution:** Disable MMU on BOTH CPUs before any memory operations.

### Problem: DSCR Timeout / Debug Interface Corruption

After resuming CPU0 and our code runs, subsequent halt attempts fail with:
```
timeout waiting for DSCR bit change
Error waiting for halt
```

The debug APB bus becomes stuck, returning stale data (0x00000013) for every
register read regardless of address. This persists across OpenOCD restarts
and requires a **board power cycle** to clear.

**Likely causes:**
1. WFE instructions in fault handlers gate the CPU clock, killing debug access
2. Data aborts during code execution corrupt the debug state machine
3. DSCR DTR_RX_FULL bit (0x4B00C002 observed) from incomplete debug transactions

**DAP-level investigation:** Used `zynq.dap apreg 1 <reg> <addr>` to directly
access the APB debug port. Could read DSCR (0x80090088) and enumerate the
CoreSight ROM table, but writes to DSCR had no effect and the CPU couldn't
be recovered without power cycle.

**CoreSight ROM table** (from DAP info):
- 0x80090000: CPU0 Debug Unit (Cortex-A9)
- 0x80091000: CPU0 PMU
- 0x80092000: CPU1 Debug Unit
- 0x80093000: CPU1 PMU
- 0x80001000: ETB (Trace Buffer)
- 0x80003000: TPIU (Trace Port)

### UART Register State (Observed via OpenOCD)

After halting with both MMUs disabled, we could read UART0 registers:

| Register         | Address    | Value  | Meaning                         |
|------------------|------------|--------|---------------------------------|
| CR (Control)     | 0xE0000000 | 0x0114 | TXEN + RXEN (u-boot config)     |
| SR (Status)      | 0xE000002C | 0x000A | TXEMPTY + RXEMPTY               |
| BRGR (Baud Gen)  | 0xE0000018 | 0x00AD | CD=173                          |
| BDIV (Baud Div)  | 0xE0000034 | 0x0004 | BDIV=4                          |

Baud rate calculation: 100 MHz / (173 * (4+1)) = 115,607 baud (~0.35% error
from 115200, well within tolerance).

The UART hardware was correctly configured by u-boot. Our init code was
**overwriting** this good configuration with bad CR values (STARTBRK).

## Current Status

- UART CR and RCC clock bit fixes committed (7ae11e4)
- All 3 targets build clean (PYNQ-Z2, F207, F407)
- Hardware verification partially complete:
  - 2 bytes received on first attempt (UART hardware path confirmed working)
  - Full message not yet verified due to pending IRQ and debug corruption issues
- **Next step:** Use Vivado-generated BSP (ps7_init) to properly initialize
  the PS, eliminating dependency on u-boot. This enables JTAG boot mode
  (no SD card) and gives us a clean CPU state without Linux residue.

## Lessons Learned

1. **Always cross-reference register definitions against the TRM.** Copy-paste
   from other files or memory is error-prone. The CrashDumpBoard.cpp had correct
   bits while Uart.cpp had wrong bits -- both written in the same session.

2. **Dual-CPU debugging is fundamentally different from single-core Cortex-M.**
   CPU1 running Linux actively interferes with debug operations. Must halt and
   neuter both CPUs before any bare-metal work.

3. **Linux leaves extensive hardware state** (GIC, timers, MMU, caches, pending
   IRQs). Simply halting and loading new code is insufficient -- must actively
   clean up the hardware state.

4. **WFE/WFI on Cortex-A9 can kill debug access.** The debug clock domain may
   be gated. Consider using infinite `b .` loops instead of WFE in early bringup
   fault handlers.

5. **The web installer approach (JTAG into running Linux) is fragile.** A proper
   bringup flow uses JTAG boot mode + ps7_init, giving full control from reset
   with no Linux residue to clean up.

## Reference Documents

- [Zynq-7000 TRM (UG585)](https://docs.amd.com/r/en-US/ug585-zynq-7000-SoC-TRM) -- primary reference
- `/mnt/data/sandbox/embedded/rtos/ug585-Zynq-7000-TRM.pdf` -- local PDF (2014 version, prefer online)
- `/mnt/data/sandbox/embedded/rtos/cortex_a9_mpcore_trm_100486_0401_10_en.pdf` -- A9 MPCore TRM
- `/mnt/data/sandbox/embedded/rtos/DDI0388G_cortex_a9_r3p0_trm.pdf` -- A9 core TRM

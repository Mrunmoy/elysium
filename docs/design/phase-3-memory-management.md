# Phase 3: Memory Management -- Design Document

## Overview

Phase 3 adds three memory management components to ms-os: a fixed-size block
pool allocator, a variable-size heap allocator, and MPU (Memory Protection Unit)
configuration. Together these provide deterministic kernel-internal allocation,
dynamic C++ application allocation, and hardware memory protection.

---

## High-Level Architecture

```
 Application Code (new/delete, std::vector, etc.)
        |
        v
 +-------------------+
 | operator new/     |     Routes C++ allocation to kernel heap
 | operator delete   |
 | (HeapOperators)   |
 +-------------------+
        |
        v
 +-------------------+       +-------------------+
 | Heap Allocator    |       | Block Pool        |
 | (first-fit,       |       | (fixed-size,      |
 |  coalescing)      |       |  O(1) alloc/free) |
 | For variable-size |       | For kernel objects |
 +-------------------+       +-------------------+
        |
        v
 +-------------------+
 | MPU Configuration |
 | (6 of 8 regions)  |
 | Per-thread stack   |
 | region updated on  |
 | context switch     |
 +-------------------+
        |
        v
 ARM Hardware (Cortex-M3/M4 MPU, or stub on Cortex-A9)
```

---

## Block Pool Allocator

### Purpose

Fixed-size block pool for kernel-internal allocation where block size is
known at compile time (e.g., TCBs, message buffers, timer control blocks).
Provides O(1) deterministic allocation suitable for ISR context.

### Design

**Embedded free-list:** Each free block stores a pointer to the next free
block in its first `sizeof(void*)` bytes. No separate metadata table needed.

```
  +--------+    +--------+    +--------+    +--------+
  | next --+--->| next --+--->| next --+--->| NULL   |
  | (free) |    | (free) |    | (free) |    | (free) |
  +--------+    +--------+    +--------+    +--------+
     ^
     |
  freeHead
```

**Minimum block size:** `sizeof(void*)` (4 bytes on ARM 32-bit), since each
free block must hold a next pointer.

### Data Structure

```cpp
class BlockPool {
    void *m_freeHead;              // Head of free list (LIFO stack)
    std::uint8_t *m_bufferStart;   // Start of backing buffer
    std::uint8_t *m_bufferEnd;     // End of backing buffer
    std::uint32_t m_blockSize;     // Aligned block size
    std::uint32_t m_totalBlocks;   // Total blocks in pool
    std::uint32_t m_freeBlocks;    // Currently free
    std::uint32_t m_minFreeBlocks; // Low-watermark (monitoring)
};
```

### Operations

| Operation    | Complexity | Description                                |
|-------------|------------|--------------------------------------------|
| `init()`    | O(n)       | Chain all blocks into free list             |
| `allocate()`| O(1)       | Pop head of free list                       |
| `free(ptr)` | O(1)       | Push to head (with bounds + alignment check)|
| `stats()`   | O(1)       | Return counters                             |

### Validation on `free()`

1. Null check (reject)
2. Bounds check: ptr within [bufferStart, bufferEnd)
3. Alignment check: (ptr - bufferStart) % blockSize == 0
4. Returns `false` on invalid pointer (does not crash)

---

## Heap Allocator

### Purpose

Variable-size dynamic allocation for user application code. Supports C++
`new`/`delete`, `std::vector`, `std::unique_ptr`, and other standard library
containers. Not intended for kernel-internal use (use BlockPool instead).

### Algorithm: First-Fit with Immediate Coalescing

Based on the free-list approach described in OSTEP Chapter 17. Key properties:

- **First-fit search:** Walk free list until finding a block >= requested size
- **Block splitting:** If remainder >= minimum block size, split into two blocks
- **Immediate coalescing:** On free, merge with adjacent free blocks immediately
- **Address-sorted free list:** Free blocks kept in address order for O(1)
  adjacency detection during coalescing

### Block Header

```
 +------------------+
 | size (31 bits)   |  Bit 0 = allocated flag
 | next (32 bits)   |  Pointer to next free block (valid only when free)
 +------------------+
 | user data...     |  Returned pointer points here (8-byte aligned)
 |                  |
 +------------------+
```

- Header size: 8 bytes
- Minimum block size: 8 bytes (header only, no payload)
- All sizes and pointers 8-byte aligned (ARM AAPCS)
- Bit 0 of `size` encodes allocated (1) vs free (0)

### Sentinel End-Block

A sentinel block is placed at the end of the heap with size=0 and
allocated=1. This prevents coalescing past the heap boundary without
special-case checks.

### Free List Structure

```
  s_freeListHead (sentinel)
       |
       v
  +--------+    +--------+    +--------+    +--------+
  | size=0 |    | size=64|    | size=128    | size=0 |
  | next --+--->| next --+--->| next --+--->| NULL   |  (sentinel end)
  | (head) |    | (free) |    | (free) |    | (end)  |
  +--------+    +--------+    +--------+    +--------+
                  0x2000        0x2100
                (address-sorted: 0x2000 < 0x2100)
```

### Allocation Flow

```
heapAlloc(requestedSize):
  1. Round up to 8-byte alignment, add header size
  2. Walk free list (first-fit)
  3. If block found:
     a. If (blockSize - needed) >= minBlockSize:
        - Split: current block = needed, create remainder block
        - Insert remainder into free list at correct address position
     b. Remove block from free list
     c. Set allocated bit in size field
     d. Update stats (usedSize, highWatermark, allocCount)
     e. Return pointer past header
  4. If no block found: return nullptr
```

### Free Flow with Coalescing

```
heapFree(ptr):
  1. Compute header address (ptr - headerSize)
  2. Validate: in bounds, allocated bit set
  3. Clear allocated bit
  4. Find insertion point in address-sorted free list
  5. Coalesce with next neighbor:
     if (block + blockSize) == nextFreeBlock:
       merge (add next's size to block, skip next in list)
  6. Coalesce with previous neighbor:
     if (prev + prevSize) == block:
       merge (add block's size to prev)
  7. Update stats
```

### Statistics

```cpp
struct HeapStats {
    std::uint32_t totalSize;         // Total heap region size
    std::uint32_t usedSize;          // Currently allocated
    std::uint32_t freeSize;          // Currently free
    std::uint32_t highWatermark;     // Peak usedSize
    std::uint32_t allocCount;        // Active allocations
    std::uint32_t largestFreeBlock;  // Largest contiguous free block
};
```

---

## C++ Operator Overloads (HeapOperators.cpp)

Cross-compile only (not included in host test builds). Routes all C++
dynamic allocation through the kernel heap:

```cpp
void *operator new(std::size_t size)      { return heapAlloc(size); }
void *operator new[](std::size_t size)    { return heapAlloc(size); }
void  operator delete(void *ptr)          { heapFree(ptr); }
void  operator delete[](void *ptr)        { heapFree(ptr); }
```

Also provides a `_sbrk()` stub that returns -1, preventing newlib's malloc
from competing with our heap.

---

## Memory Protection Unit (MPU)

### Purpose

Hardware enforcement of memory access rules. Prevents threads from
corrupting kernel memory, other threads' stacks, or executing data as code.

### Region Layout (6 of 8 ARMv7-M MPU Regions Used)

| Region | Name        | Base         | Size    | AP          | XN  | Type             |
|--------|-------------|------------- |---------|-------------|-----|------------------|
| 0      | Flash       | 0x08000000   | 512K/1M | Priv+Unpriv RO | No  | Normal WT     |
| 1      | Kernel SRAM | 0x20000000   | 128K    | Priv RW only| Yes | Normal non-cache |
| 2      | Peripherals | 0x40000000   | 512M    | Priv RW only| Yes | Device           |
| 3      | System      | 0xE0000000   | 512M    | Priv RW only| Yes | Strongly ordered |
| 4      | Thread Stack| (per-thread) | varies  | Full access | Yes | Normal non-cache |
| 5      | Heap        | (from linker)| 16K     | Full access | Yes | Normal non-cache |
| 6-7    | Reserved    | --           | --      | --          | --  | Future use       |

### MPU Control Register Settings

- **PRIVDEFENA = 1:** Privileged code gets the default memory map for
  unmapped regions. This allows the kernel to access all memory even if
  no explicit MPU region covers it.
- **HFNMIENA = 0:** MPU is disabled during HardFault/NMI. This ensures
  the crash dump system can access all memory for register/stack dumps.

### Thread Stack Region (Region 4, Dynamic)

Updated on every context switch. Pre-computed RBAR/RASR values stored in
the TCB at offsets 36 and 40 to minimize context switch overhead:

```
TCB:
  +36: mpuStackRbar    (RBAR: base address | VALID | region 4)
  +40: mpuStackRasr    (RASR: AP=full | XN | SIZE | ENABLE)
```

Context switch assembly (4 extra instructions, ~50 ns at 120 MHz):
```asm
  ldr r0, [r2, #36]       ; Load RBAR from next TCB
  ldr r1, [r2, #40]       ; Load RASR from next TCB
  ldr r3, =0xE000ED9C     ; MPU->RBAR address
  stm r3, {r0, r1}        ; Write both registers atomically
```

### Stack Requirements for MPU

- Minimum 32 bytes
- Must be power-of-2 size (MPU hardware requirement)
- Must be aligned to its own size (base address % size == 0)
- Use `alignas(stackSize)` on static stack arrays

```cpp
// Valid: 512 bytes, aligned to 512
alignas(512) static std::uint32_t stack[128];

// Invalid: not power-of-2
static std::uint32_t stack[100];  // 400 bytes, not power-of-2
```

### MPU Helper Functions

```cpp
std::uint32_t mpuRoundUpSize(std::uint32_t size);
    // Round up to next power of 2 (minimum 32)

std::uint8_t mpuSizeEncoding(std::uint32_t size);
    // SIZE field = log2(size) - 1

bool mpuValidateStack(const void *base, std::uint32_t size);
    // Check minimum size, power-of-2, alignment

ThreadMpuConfig mpuComputeThreadConfig(const void *base, std::uint32_t size);
    // Pre-compute RBAR/RASR for storing in TCB
```

### Cortex-A9 (Stub)

The Cortex-A9 port uses MMU instead of MPU. The `Mpu.cpp` for cortex-a9
provides stub implementations (no-ops) for `mpuInit()` and
`mpuConfigureThreadRegion()`. The portable math functions (`mpuRoundUpSize`,
`mpuSizeEncoding`, `mpuValidateStack`) are retained.

---

## Memory Layout (STM32F207ZGT6)

```
FLASH (1 MB): 0x08000000
  +------------------+
  | .isr_vector      |  Interrupt vector table
  | .text            |  Code
  | .rodata          |  Constants
  | .ARM.exidx       |  Exception unwinding
  | .init_array      |  C++ static constructors
  | (.data LMA)      |  Initial values for .data
  +------------------+

SRAM (128 KB): 0x20000000
  +------------------+  0x20000000
  | .data            |  Initialized globals (copied from Flash)
  | .bss             |  Uninitialized globals (zeroed)
  +------------------+
  | .heap (16K)      |  <-- aligned to 16K for MPU Region 5
  | _heap_start      |
  | ...              |
  | _heap_end        |
  +------------------+
  | (unused SRAM)    |
  +------------------+
  | ._stack (4K)     |  Main stack (MSP), grows downward
  | _estack          |  0x20020000
  +------------------+  0x20020000
```

The heap is aligned to 16K in the linker script so that a single MPU region
can cover it exactly (power-of-2 requirement).

---

## Integration with Scheduler

### Thread Creation

When `threadCreate()` is called:

1. Validate stack meets MPU requirements (`mpuValidateStack`)
2. Pre-compute MPU region config (`mpuComputeThreadConfig`)
3. Store RBAR/RASR in TCB fields `mpuStackRbar` / `mpuStackRasr`
4. Build initial stack frame (16 words)

### Context Switch

The PendSV handler (Cortex-M) or IRQ handler (Cortex-A9) performs:

1. Save outgoing thread's registers and SP
2. **Write new thread's MPU RBAR/RASR** (Cortex-M only)
3. Load incoming thread's SP and registers
4. Return to new thread

### Kernel Initialization

```cpp
void kernel::init()
{
    crashDumpInit();                          // Crash dump subsystem
    heapInit(&_heap_start, &_heap_end);       // Initialize heap from linker symbols
    mpuInit();                                // Configure 6 static MPU regions
    threadReset();                            // Clear TCB pool
    s_scheduler.init();                       // Initialize scheduler
    // Create idle thread...
}
```

---

## Thread Safety

All allocators are protected by critical sections:

```cpp
void *heapAlloc(std::uint32_t size)
{
    arch::enterCritical();
    // ... walk free list, allocate ...
    arch::exitCritical();
    return ptr;
}
```

This means:
- Multiple threads can safely call `new`/`delete` concurrently
- ISRs can technically allocate/free (though discouraged)
- Heap free is O(n) in the number of free blocks, so critical section
  duration is bounded by fragmentation level
- BlockPool operations are always O(1) critical sections

---

## API Summary

### Block Pool

```cpp
class BlockPool {
    void init(void *buffer, std::uint32_t blockSize, std::uint32_t blockCount);
    void *allocate();
    bool free(void *ptr);
    BlockPoolStats stats() const;
    void reset();
};
```

### Heap

```cpp
void heapInit(void *start, void *end);
void *heapAlloc(std::uint32_t size);
void heapFree(void *ptr);
HeapStats heapGetStats();
void heapReset();
```

### MPU

```cpp
void mpuInit();
void mpuConfigureThreadRegion(const ThreadMpuConfig &config);
ThreadMpuConfig mpuComputeThreadConfig(const void *base, std::uint32_t size);
bool mpuValidateStack(const void *base, std::uint32_t size);
std::uint32_t mpuRoundUpSize(std::uint32_t size);
std::uint8_t mpuSizeEncoding(std::uint32_t size);
```

---

## Design Decisions

| Decision                          | Rationale                                      |
|-----------------------------------|-------------------------------------------------|
| Embedded free-list (BlockPool)    | Zero metadata overhead, O(1) operations         |
| First-fit (Heap)                  | Simple, good average-case fragmentation         |
| Immediate coalescing              | Prevents fragmentation accumulation             |
| Address-sorted free list          | O(1) adjacency detection for coalescing         |
| 8-byte alignment                  | ARM AAPCS requirement for double-word access    |
| Sentinel end-block                | Eliminates boundary checks during coalescing    |
| Pre-computed MPU config in TCB    | Minimizes context switch overhead (~50 ns)      |
| PRIVDEFENA = 1                    | Privileged kernel code can access everything    |
| HFNMIENA = 0                     | Crash dump can read all memory during faults    |
| Static heap from linker symbols   | Flexible per-board, no runtime configuration    |
| Threads remain privileged         | Unprivileged mode deferred to future phase      |

---

## Files

### Public Headers
- `kernel/inc/kernel/BlockPool.h`
- `kernel/inc/kernel/Heap.h`
- `kernel/inc/kernel/Mpu.h`

### Core Implementation
- `kernel/src/core/BlockPool.cpp`
- `kernel/src/core/Heap.cpp`
- `kernel/src/core/HeapOperators.cpp` (cross-compile only)

### Architecture-Specific
- `kernel/src/arch/cortex-m3/Mpu.cpp` -- ARMv7-M MPU regions
- `kernel/src/arch/cortex-m4/Mpu.cpp` -- same as M3 (different flash size)
- `kernel/src/arch/cortex-a9/Mpu.cpp` -- stub (no-ops, MMU deferred)

### Tests
- `test/kernel/BlockPoolTest.cpp`
- `test/kernel/HeapTest.cpp`
- `test/kernel/MpuTest.cpp`

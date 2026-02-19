// Thread management: TCB pool and thread creation with initial stack frame.
// This module has zero hardware dependencies -- fully testable on host.

#include "kernel/Thread.h"
#include "kernel/CortexM.h"

#include <cstdint>
#include <cstring>

namespace kernel
{
    // Static TCB pool
    static ThreadControlBlock s_tcbPool[kMaxThreads];
    static ThreadId s_nextId = 0;

    void threadReset()
    {
        std::memset(s_tcbPool, 0, sizeof(s_tcbPool));
        s_nextId = 0;
        for (std::uint8_t i = 0; i < kMaxThreads; ++i)
        {
            s_tcbPool[i].m_state = ThreadState::Inactive;
            s_tcbPool[i].m_id = kInvalidThreadId;
        }
    }

    ThreadId threadCreate(const ThreadConfig &config)
    {
        if (s_nextId >= kMaxThreads)
        {
            return kInvalidThreadId;
        }

        ThreadId id = s_nextId++;
        ThreadControlBlock &tcb = s_tcbPool[id];

        tcb.m_id = id;
        tcb.m_state = ThreadState::Ready;
        tcb.m_priority = config.priority;
        tcb.m_name = config.name;
        tcb.m_stackBase = config.stack;
        tcb.m_stackSize = config.stackSize;
        tcb.m_timeSlice = (config.timeSlice == 0) ? kDefaultTimeSlice : config.timeSlice;
        tcb.m_timeSliceRemaining = tcb.m_timeSlice;

        // Build initial stack frame at top of stack
        // Stack grows downward; top = base + (size / sizeof(uint32_t))
        std::uint32_t *stackTop = config.stack + (config.stackSize / sizeof(std::uint32_t));

        // Align stack top to 8 bytes (ARM AAPCS requirement)
        stackTop = reinterpret_cast<std::uint32_t *>(
            reinterpret_cast<std::uintptr_t>(stackTop) & ~std::uintptr_t{7});

        // Build the 16-word initial stack frame (64 bytes, naturally 8-byte aligned)
        // Layout from high to low address:
        //   xPSR, PC, LR, r12, r3, r2, r1, r0   (8 words: hw exception frame)
        //   r11, r10, r9, r8, r7, r6, r5, r4     (8 words: sw-saved context)
        //
        // EXC_RETURN is NOT on the stack -- PendSV/SVC handlers use hardcoded
        // 0xFFFFFFFD (thread mode, PSP, no FPU) since Cortex-M3 has no FPU.

        stackTop -= 16;

        // Software-saved context (pushed/popped by PendSV handler)
        stackTop[0] = 0;                                                   // r4
        stackTop[1] = 0;                                                   // r5
        stackTop[2] = 0;                                                   // r6
        stackTop[3] = 0;                                                   // r7
        stackTop[4] = 0;                                                   // r8
        stackTop[5] = 0;                                                   // r9
        stackTop[6] = 0;                                                   // r10
        stackTop[7] = 0;                                                   // r11

        // Hardware exception frame (popped automatically on exception return)
        // Cast through uintptr_t for portability (truncates on 64-bit host, exact on ARM)
        stackTop[8] = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(config.arg));                 // r0 = arg
        stackTop[9] = 0;                                                   // r1
        stackTop[10] = 0;                                                  // r2
        stackTop[11] = 0;                                                  // r3
        stackTop[12] = 0;                                                  // r12
        stackTop[13] = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(&kernelThreadExit));          // LR
        stackTop[14] = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(config.function));            // PC
        stackTop[15] = 0x01000000u;                                        // xPSR (Thumb)

        tcb.m_stackPointer = stackTop;

        return id;
    }

    ThreadControlBlock *threadGetTcb(ThreadId id)
    {
        if (id >= kMaxThreads)
        {
            return nullptr;
        }
        return &s_tcbPool[id];
    }

    ThreadControlBlock *threadGetTcbArray()
    {
        return s_tcbPool;
    }

}  // namespace kernel

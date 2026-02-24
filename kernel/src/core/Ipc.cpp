// Kernel IPC: message passing between threads.
//
// Each thread has a mailbox with a ring buffer of message slots.
// Blocking follows the same pattern as Mutex/Semaphore:
//   enterCritical -> waitQueueInsert -> blockCurrentThread ->
//   switchContext -> exitCritical -> triggerContextSwitch
//
// Notifications are ISR-safe: a single 32-bit OR into notifyBits.

#include "kernel/Ipc.h"
#include "kernel/Scheduler.h"
#include "kernel/Arch.h"
#include "WaitQueue.h"

#include <cstring>

namespace kernel
{
    extern ThreadControlBlock *g_currentTcb;
    extern ThreadControlBlock *g_nextTcb;

    namespace internal
    {
        Scheduler &scheduler();
    }

    static ThreadMailbox s_mailboxes[kMaxThreads];

    // ---- Init / reset ----

    void ipcInit()
    {
        ipcReset();
    }

    void ipcReset()
    {
        std::memset(s_mailboxes, 0, sizeof(s_mailboxes));
        for (std::uint8_t i = 0; i < kMaxThreads; ++i)
        {
            s_mailboxes[i].senderWaitHead = kInvalidThreadId;
            s_mailboxes[i].receiverWaitHead = kInvalidThreadId;
            s_mailboxes[i].blockReason = IpcBlockReason::None;
            s_mailboxes[i].replySlot = nullptr;
        }
    }

    void ipcResetMailbox(ThreadId id)
    {
        if (id >= kMaxThreads)
        {
            return;
        }
        ThreadMailbox &box = s_mailboxes[id];
        std::memset(&box, 0, sizeof(box));
        box.senderWaitHead = kInvalidThreadId;
        box.receiverWaitHead = kInvalidThreadId;
        box.blockReason = IpcBlockReason::None;
        box.replySlot = nullptr;
    }

    ThreadMailbox *ipcGetMailbox(ThreadId id)
    {
        if (id >= kMaxThreads)
        {
            return nullptr;
        }
        return &s_mailboxes[id];
    }

    // ---- Internal helpers ----

    // Enqueue a message into a mailbox. Caller must hold critical section.
    // Returns true if enqueued, false if full.
    static bool mailboxEnqueue(ThreadMailbox &box, const Message &msg, ThreadId sender)
    {
        if (box.count >= kMailboxDepth)
        {
            return false;
        }
        Message &slot = box.slots[box.tail];
        slot = msg;
        slot.sender = sender;
        box.tail = static_cast<std::uint8_t>((box.tail + 1) % kMailboxDepth);
        ++box.count;
        return true;
    }

    // Dequeue a message from a mailbox. Caller must hold critical section.
    // Returns true if dequeued, false if empty.
    static bool mailboxDequeue(ThreadMailbox &box, Message *msg)
    {
        if (box.count == 0)
        {
            return false;
        }
        *msg = box.slots[box.head];
        box.head = static_cast<std::uint8_t>((box.head + 1) % kMailboxDepth);
        --box.count;
        return true;
    }

    // Perform context switch bookkeeping (same pattern as Mutex/Semaphore).
    static void doSwitchContext()
    {
        Scheduler &sched = internal::scheduler();
        ThreadId nextId = sched.switchContext();
        ThreadControlBlock *nextTcb = threadGetTcb(nextId);
        if (nextTcb != nullptr)
        {
            g_nextTcb = nextTcb;
        }
    }

    // ---- Synchronous message passing ----

    std::int32_t messageSend(ThreadId dest, const Message &msg, Message *reply)
    {
        if (arch::inIsrContext())
        {
            return kIpcErrIsr;
        }
        if (dest >= kMaxThreads)
        {
            return kIpcErrNoThread;
        }
        if (reply == nullptr)
        {
            return kIpcErrInvalid;
        }

        ThreadControlBlock *destTcb = threadGetTcb(dest);
        if (destTcb == nullptr || destTcb->state == ThreadState::Inactive)
        {
            return kIpcErrNoThread;
        }

        arch::enterCritical();

        Scheduler &sched = internal::scheduler();
        ThreadId currentId = sched.currentThreadId();
        ThreadMailbox &destBox = s_mailboxes[dest];
        ThreadMailbox &myBox = s_mailboxes[currentId];

        // If dest mailbox is full, block on its senderWait queue
        if (destBox.count >= kMailboxDepth)
        {
            myBox.blockReason = IpcBlockReason::Send;
            waitQueueInsert(destBox.senderWaitHead, currentId);
            ++destBox.senderWaitCount;

            sched.blockCurrentThread();
            doSwitchContext();
            arch::exitCritical();
            arch::triggerContextSwitch();

            // Resumed -- space should be available now
            arch::enterCritical();
        }

        // Enqueue message
        mailboxEnqueue(destBox, msg, currentId);

        // If receiver is blocked waiting for a message, unblock it
        if (!waitQueueEmpty(destBox.receiverWaitHead))
        {
            ThreadId receiverId = waitQueueRemoveHead(destBox.receiverWaitHead);
            --destBox.receiverWaitCount;
            s_mailboxes[receiverId].blockReason = IpcBlockReason::None;
            sched.unblockThread(receiverId);
        }

        // Block waiting for reply
        myBox.blockReason = IpcBlockReason::Reply;
        myBox.replySlot = reply;

        sched.blockCurrentThread();
        doSwitchContext();
        arch::exitCritical();
        arch::triggerContextSwitch();

        // When we resume, reply has been filled by messageReply()
        return kIpcOk;
    }

    std::int32_t messageReceive(Message *msg)
    {
        if (arch::inIsrContext())
        {
            return kIpcErrIsr;
        }
        if (msg == nullptr)
        {
            return kIpcErrInvalid;
        }

        arch::enterCritical();

        Scheduler &sched = internal::scheduler();
        ThreadId currentId = sched.currentThreadId();
        ThreadMailbox &myBox = s_mailboxes[currentId];

        if (myBox.count > 0)
        {
            // Message available -- dequeue it
            mailboxDequeue(myBox, msg);

            // If a sender was blocked waiting for space, unblock it
            if (!waitQueueEmpty(myBox.senderWaitHead))
            {
                ThreadId senderId = waitQueueRemoveHead(myBox.senderWaitHead);
                --myBox.senderWaitCount;
                s_mailboxes[senderId].blockReason = IpcBlockReason::None;
                sched.unblockThread(senderId);
            }

            arch::exitCritical();
            return kIpcOk;
        }

        // No messages -- block
        myBox.blockReason = IpcBlockReason::Receive;
        waitQueueInsert(myBox.receiverWaitHead, currentId);
        ++myBox.receiverWaitCount;

        sched.blockCurrentThread();
        doSwitchContext();
        arch::exitCritical();
        arch::triggerContextSwitch();

        // Resumed -- message should be available now
        arch::enterCritical();
        mailboxDequeue(myBox, msg);

        // Wake any blocked sender now that there is space
        if (!waitQueueEmpty(myBox.senderWaitHead))
        {
            ThreadId senderId = waitQueueRemoveHead(myBox.senderWaitHead);
            --myBox.senderWaitCount;
            s_mailboxes[senderId].blockReason = IpcBlockReason::None;
            sched.unblockThread(senderId);
        }

        arch::exitCritical();
        return kIpcOk;
    }

    std::int32_t messageReply(ThreadId dest, const Message &reply)
    {
        if (dest >= kMaxThreads)
        {
            return kIpcErrNoThread;
        }

        ThreadControlBlock *destTcb = threadGetTcb(dest);
        if (destTcb == nullptr)
        {
            return kIpcErrNoThread;
        }

        arch::enterCritical();

        Scheduler &sched = internal::scheduler();
        ThreadMailbox &destBox = s_mailboxes[dest];

        // The dest thread should be blocked waiting for a reply
        if (destBox.blockReason != IpcBlockReason::Reply || destBox.replySlot == nullptr)
        {
            arch::exitCritical();
            return kIpcErrInvalid;
        }

        // Copy reply into the sender's reply slot
        *destBox.replySlot = reply;
        destBox.replySlot = nullptr;
        destBox.blockReason = IpcBlockReason::None;

        // Unblock the sender
        bool preempt = sched.unblockThread(dest);

        if (preempt)
        {
            doSwitchContext();
            arch::exitCritical();
            arch::triggerContextSwitch();
        }
        else
        {
            arch::exitCritical();
        }

        return kIpcOk;
    }

    // ---- Non-blocking variants ----

    std::int32_t messageTrySend(ThreadId dest, const Message &msg)
    {
        if (dest >= kMaxThreads)
        {
            return kIpcErrNoThread;
        }

        ThreadControlBlock *destTcb = threadGetTcb(dest);
        if (destTcb == nullptr || destTcb->state == ThreadState::Inactive)
        {
            return kIpcErrNoThread;
        }

        arch::enterCritical();

        Scheduler &sched = internal::scheduler();
        ThreadId currentId = sched.currentThreadId();
        ThreadMailbox &destBox = s_mailboxes[dest];

        if (destBox.count >= kMailboxDepth)
        {
            arch::exitCritical();
            return kIpcErrFull;
        }

        mailboxEnqueue(destBox, msg, currentId);

        // Wake receiver if blocked
        if (!waitQueueEmpty(destBox.receiverWaitHead))
        {
            ThreadId receiverId = waitQueueRemoveHead(destBox.receiverWaitHead);
            --destBox.receiverWaitCount;
            s_mailboxes[receiverId].blockReason = IpcBlockReason::None;
            bool preempt = sched.unblockThread(receiverId);
            if (preempt)
            {
                doSwitchContext();
                arch::exitCritical();
                arch::triggerContextSwitch();
                return kIpcOk;
            }
        }

        arch::exitCritical();
        return kIpcOk;
    }

    std::int32_t messageTryReceive(Message *msg)
    {
        if (msg == nullptr)
        {
            return kIpcErrInvalid;
        }

        arch::enterCritical();

        Scheduler &sched = internal::scheduler();
        ThreadId currentId = sched.currentThreadId();
        ThreadMailbox &myBox = s_mailboxes[currentId];

        if (myBox.count == 0)
        {
            arch::exitCritical();
            return kIpcErrEmpty;
        }

        mailboxDequeue(myBox, msg);

        // Wake blocked sender if any
        if (!waitQueueEmpty(myBox.senderWaitHead))
        {
            ThreadId senderId = waitQueueRemoveHead(myBox.senderWaitHead);
            --myBox.senderWaitCount;
            s_mailboxes[senderId].blockReason = IpcBlockReason::None;
            sched.unblockThread(senderId);
        }

        arch::exitCritical();
        return kIpcOk;
    }

    // ---- Async notifications ----

    std::int32_t messageNotify(ThreadId dest, std::uint32_t bits)
    {
        if (dest >= kMaxThreads)
        {
            return kIpcErrNoThread;
        }
        if (bits == 0)
        {
            return kIpcErrInvalid;
        }

        arch::enterCritical();

        s_mailboxes[dest].notifyBits |= bits;

        arch::exitCritical();
        return kIpcOk;
    }

    std::uint32_t messageCheckNotify()
    {
        arch::enterCritical();

        Scheduler &sched = internal::scheduler();
        ThreadId currentId = sched.currentThreadId();
        ThreadMailbox &myBox = s_mailboxes[currentId];

        std::uint32_t bits = myBox.notifyBits;
        myBox.notifyBits = 0;

        arch::exitCritical();
        return bits;
    }

}  // namespace kernel

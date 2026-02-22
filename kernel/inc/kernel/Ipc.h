#pragma once

// Kernel IPC: message passing between threads.
//
// Each thread has a statically allocated mailbox with a small ring buffer
// of message slots.  Senders address messages by ThreadId.  The model
// follows the Minix send/receive/reply pattern:
//
//   Client: messageSend(serverTid, &msg)       -- blocks until delivered
//   Server: messageReceive(&msg)               -- blocks until message arrives
//   Server: messageReply(clientTid, &reply)     -- unblocks the client
//
// Async notifications use a per-thread 32-bit bitmask (ISR-safe, no copy).

#include "kernel/Thread.h"

#include <cstdint>

namespace kernel
{
    // Maximum inline payload per message (48 bytes)
    static constexpr std::uint8_t kMaxPayloadSize = 48;

    // Number of message slots per thread mailbox
    static constexpr std::uint8_t kMailboxDepth = 4;

    // IPC status codes
    static constexpr std::int32_t kIpcOk           =  0;
    static constexpr std::int32_t kIpcErrInvalid   = -1;  // Invalid argument
    static constexpr std::int32_t kIpcErrFull      = -2;  // Mailbox full (non-blocking)
    static constexpr std::int32_t kIpcErrEmpty     = -3;  // Mailbox empty (non-blocking)
    static constexpr std::int32_t kIpcErrNoThread  = -4;  // Destination thread invalid
    static constexpr std::int32_t kIpcErrIsr       = -5;  // Cannot block from ISR
    static constexpr std::int32_t kIpcErrMethod    = -6;  // Unknown method ID

    enum class MessageType : std::uint8_t
    {
        Request  = 1,
        Reply    = 2,
        Notify   = 3,
        OneWay   = 4,
    };

    struct Message
    {
        // Header (16 bytes)
        ThreadId sender;                  //  1B -- filled by kernel on send
        std::uint8_t type;                //  1B -- MessageType
        std::uint16_t methodId;           //  2B -- service method ID
        std::uint32_t serviceId;          //  4B -- FNV-1a hash of service name
        std::int32_t status;              //  4B -- return code (for replies)
        std::uint16_t payloadSize;        //  2B -- bytes used in payload[]
        std::uint8_t reserved[2];         //  2B -- padding

        // Payload (48 bytes)
        std::uint8_t payload[kMaxPayloadSize];
    };

    static_assert(sizeof(Message) == 64, "Message must be exactly 64 bytes");

    // IPC block reason (tracked per-mailbox, not in TCB)
    enum class IpcBlockReason : std::uint8_t
    {
        None    = 0,
        Send    = 1,  // Blocked because dest mailbox full
        Receive = 2,  // Blocked waiting for incoming message
        Reply   = 3,  // Blocked waiting for reply after send
    };

    struct ThreadMailbox
    {
        Message slots[kMailboxDepth];     // Inline ring buffer
        std::uint8_t head;                // Read index
        std::uint8_t tail;                // Write index
        std::uint8_t count;               // Number of pending messages
        std::uint8_t pad;
        std::uint32_t notifyBits;         // Async notification bitmask
        ThreadId senderWaitHead;          // Threads blocked trying to send
        ThreadId receiverWaitHead;        // Threads blocked on receive (only 1)
        std::uint8_t senderWaitCount;
        std::uint8_t receiverWaitCount;
        IpcBlockReason blockReason;       // Why the owning thread is blocked
        Message *replySlot;               // Where to write reply for send
    };

    // ---- Synchronous message passing ----

    // Send a message to dest's mailbox. Blocks if the mailbox is full.
    // After delivery, blocks waiting for a reply via messageReply().
    // On return, the reply message is written to *reply.
    std::int32_t messageSend(ThreadId dest, const Message &msg, Message *reply);

    // Receive a message from the current thread's mailbox.
    // Blocks if no message is pending.
    std::int32_t messageReceive(Message *msg);

    // Send a reply to a thread that called messageSend().
    // Copies the reply into the sender's replySlot and unblocks the sender.
    std::int32_t messageReply(ThreadId dest, const Message &reply);

    // ---- Non-blocking variants ----

    // Try to send without blocking. Returns kIpcErrFull if mailbox full.
    // Does NOT wait for a reply.
    std::int32_t messageTrySend(ThreadId dest, const Message &msg);

    // Try to receive without blocking. Returns kIpcErrEmpty if no messages.
    std::int32_t messageTryReceive(Message *msg);

    // ---- Async notifications (ISR-safe) ----

    // Set notification bits on dest thread. Non-blocking, ISR-safe.
    std::int32_t messageNotify(ThreadId dest, std::uint32_t bits);

    // Check and clear pending notification bits for current thread.
    std::uint32_t messageCheckNotify();

    // ---- Init / test support ----

    void ipcInit();
    void ipcReset();
    ThreadMailbox *ipcGetMailbox(ThreadId id);

}  // namespace kernel

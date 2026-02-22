#include <gtest/gtest.h>

#include "kernel/Ipc.h"
#include "kernel/Scheduler.h"
#include "kernel/Thread.h"
#include "kernel/Arch.h"

#include "MockKernel.h"

#include <cstring>

namespace
{
    void dummyThread(void *arg)
    {
        (void)arg;
    }

    alignas(8) std::uint32_t g_stack1[128];
    alignas(8) std::uint32_t g_stack2[128];
}  // namespace

class IpcTest : public ::testing::Test
{
protected:
    kernel::Scheduler &m_scheduler = kernel::internal::scheduler();

    void SetUp() override
    {
        test::resetKernelMockState();
        kernel::threadReset();
        kernel::ipcReset();
        m_scheduler.init();
        kernel::g_currentTcb = nullptr;
        kernel::g_nextTcb = nullptr;
    }

    kernel::ThreadId createThread(const char *name, std::uint32_t *stack,
                                  std::uint32_t stackSize,
                                  std::uint8_t priority = kernel::kDefaultPriority)
    {
        kernel::ThreadConfig config{};
        config.function = dummyThread;
        config.arg = nullptr;
        config.name = name;
        config.stack = stack;
        config.stackSize = stackSize;
        config.priority = priority;
        return kernel::threadCreate(config);
    }

    void makeRunning(kernel::ThreadId id)
    {
        m_scheduler.addThread(id);
        m_scheduler.switchContext();
    }

    void forceCurrent(kernel::ThreadId id)
    {
        m_scheduler.setCurrentThread(id);
        kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(id);
        if (tcb != nullptr)
        {
            tcb->state = kernel::ThreadState::Running;
        }
    }

    kernel::Message makeRequest(std::uint32_t serviceId, std::uint16_t methodId,
                                const void *payload = nullptr,
                                std::uint16_t payloadSize = 0)
    {
        kernel::Message msg{};
        msg.type = static_cast<std::uint8_t>(kernel::MessageType::Request);
        msg.serviceId = serviceId;
        msg.methodId = methodId;
        msg.payloadSize = payloadSize;
        if (payload != nullptr && payloadSize > 0)
        {
            std::memcpy(msg.payload, payload, payloadSize);
        }
        return msg;
    }
};

// ---- Init / Reset ----

TEST_F(IpcTest, Init_MailboxesEmpty)
{
    for (kernel::ThreadId i = 0; i < kernel::kMaxThreads; ++i)
    {
        kernel::ThreadMailbox *box = kernel::ipcGetMailbox(i);
        ASSERT_NE(box, nullptr);
        EXPECT_EQ(box->count, 0u);
        EXPECT_EQ(box->head, 0u);
        EXPECT_EQ(box->tail, 0u);
        EXPECT_EQ(box->notifyBits, 0u);
        EXPECT_EQ(box->senderWaitHead, kernel::kInvalidThreadId);
        EXPECT_EQ(box->receiverWaitHead, kernel::kInvalidThreadId);
    }
}

TEST_F(IpcTest, GetMailbox_InvalidId_ReturnsNull)
{
    EXPECT_EQ(kernel::ipcGetMailbox(kernel::kMaxThreads), nullptr);
    EXPECT_EQ(kernel::ipcGetMailbox(0xFF), nullptr);
}

// ---- Non-blocking send/receive ----

TEST_F(IpcTest, TrySend_SucceedsToValidThread)
{
    kernel::ThreadId t1 = createThread("sender", g_stack1, sizeof(g_stack1), 10);
    kernel::ThreadId t2 = createThread("receiver", g_stack2, sizeof(g_stack2), 10);
    makeRunning(t1);
    m_scheduler.addThread(t2);

    kernel::Message msg = makeRequest(0x1234, 1);
    std::int32_t rc = kernel::messageTrySend(t2, msg);
    EXPECT_EQ(rc, kernel::kIpcOk);

    kernel::ThreadMailbox *box = kernel::ipcGetMailbox(t2);
    EXPECT_EQ(box->count, 1u);
}

TEST_F(IpcTest, TrySend_StampsSenderField)
{
    kernel::ThreadId t1 = createThread("sender", g_stack1, sizeof(g_stack1), 10);
    kernel::ThreadId t2 = createThread("receiver", g_stack2, sizeof(g_stack2), 10);
    makeRunning(t1);
    m_scheduler.addThread(t2);

    kernel::Message msg = makeRequest(0x1234, 1);
    kernel::messageTrySend(t2, msg);

    kernel::ThreadMailbox *box = kernel::ipcGetMailbox(t2);
    EXPECT_EQ(box->slots[0].sender, t1);
}

TEST_F(IpcTest, TrySend_FailsWhenFull)
{
    kernel::ThreadId t1 = createThread("sender", g_stack1, sizeof(g_stack1), 10);
    kernel::ThreadId t2 = createThread("receiver", g_stack2, sizeof(g_stack2), 10);
    makeRunning(t1);
    m_scheduler.addThread(t2);

    kernel::Message msg = makeRequest(0x1234, 1);

    // Fill the mailbox
    for (std::uint8_t i = 0; i < kernel::kMailboxDepth; ++i)
    {
        EXPECT_EQ(kernel::messageTrySend(t2, msg), kernel::kIpcOk);
    }

    // Next send should fail
    EXPECT_EQ(kernel::messageTrySend(t2, msg), kernel::kIpcErrFull);
}

TEST_F(IpcTest, TrySend_InvalidDest_ReturnsNoThread)
{
    kernel::ThreadId t1 = createThread("sender", g_stack1, sizeof(g_stack1));
    makeRunning(t1);

    kernel::Message msg = makeRequest(0x1234, 1);
    EXPECT_EQ(kernel::messageTrySend(kernel::kInvalidThreadId, msg), kernel::kIpcErrNoThread);
}

TEST_F(IpcTest, TryReceive_SucceedsWhenPending)
{
    kernel::ThreadId t1 = createThread("sender", g_stack1, sizeof(g_stack1), 10);
    kernel::ThreadId t2 = createThread("receiver", g_stack2, sizeof(g_stack2), 10);
    makeRunning(t1);
    m_scheduler.addThread(t2);

    // Send from t1 to t2
    std::uint32_t payload = 42;
    kernel::Message sendMsg = makeRequest(0xABCD, 7, &payload, sizeof(payload));
    kernel::messageTrySend(t2, sendMsg);

    // Switch to t2 and receive
    forceCurrent(t2);
    kernel::Message recvMsg{};
    std::int32_t rc = kernel::messageTryReceive(&recvMsg);
    EXPECT_EQ(rc, kernel::kIpcOk);
    EXPECT_EQ(recvMsg.sender, t1);
    EXPECT_EQ(recvMsg.serviceId, 0xABCDu);
    EXPECT_EQ(recvMsg.methodId, 7u);
    EXPECT_EQ(recvMsg.payloadSize, sizeof(payload));

    std::uint32_t received = 0;
    std::memcpy(&received, recvMsg.payload, sizeof(received));
    EXPECT_EQ(received, 42u);
}

TEST_F(IpcTest, TryReceive_FailsWhenEmpty)
{
    kernel::ThreadId t1 = createThread("t1", g_stack1, sizeof(g_stack1));
    makeRunning(t1);

    kernel::Message msg{};
    EXPECT_EQ(kernel::messageTryReceive(&msg), kernel::kIpcErrEmpty);
}

TEST_F(IpcTest, TryReceive_NullMsg_ReturnsInvalid)
{
    kernel::ThreadId t1 = createThread("t1", g_stack1, sizeof(g_stack1));
    makeRunning(t1);

    EXPECT_EQ(kernel::messageTryReceive(nullptr), kernel::kIpcErrInvalid);
}

TEST_F(IpcTest, Mailbox_RingWraparound)
{
    kernel::ThreadId t1 = createThread("sender", g_stack1, sizeof(g_stack1), 10);
    kernel::ThreadId t2 = createThread("receiver", g_stack2, sizeof(g_stack2), 10);
    makeRunning(t1);
    m_scheduler.addThread(t2);

    // Fill and drain twice to exercise wraparound
    for (int round = 0; round < 2; ++round)
    {
        for (std::uint8_t i = 0; i < kernel::kMailboxDepth; ++i)
        {
            std::uint32_t val = round * 100 + i;
            kernel::Message msg = makeRequest(0x1234, i, &val, sizeof(val));
            EXPECT_EQ(kernel::messageTrySend(t2, msg), kernel::kIpcOk);
        }

        forceCurrent(t2);
        for (std::uint8_t i = 0; i < kernel::kMailboxDepth; ++i)
        {
            kernel::Message msg{};
            EXPECT_EQ(kernel::messageTryReceive(&msg), kernel::kIpcOk);

            std::uint32_t val = 0;
            std::memcpy(&val, msg.payload, sizeof(val));
            EXPECT_EQ(val, static_cast<std::uint32_t>(round * 100 + i));
        }
        forceCurrent(t1);
    }
}

TEST_F(IpcTest, Mailbox_FifoOrdering)
{
    kernel::ThreadId t1 = createThread("sender", g_stack1, sizeof(g_stack1), 10);
    kernel::ThreadId t2 = createThread("receiver", g_stack2, sizeof(g_stack2), 10);
    makeRunning(t1);
    m_scheduler.addThread(t2);

    // Send messages with different method IDs
    for (std::uint16_t i = 0; i < kernel::kMailboxDepth; ++i)
    {
        kernel::Message msg = makeRequest(0x1234, i + 1);
        kernel::messageTrySend(t2, msg);
    }

    // Receive in order
    forceCurrent(t2);
    for (std::uint16_t i = 0; i < kernel::kMailboxDepth; ++i)
    {
        kernel::Message msg{};
        kernel::messageTryReceive(&msg);
        EXPECT_EQ(msg.methodId, i + 1);
    }
}

// ---- Blocking receive ----

TEST_F(IpcTest, Receive_BlocksWhenEmpty)
{
    kernel::ThreadId t1 = createThread("t1", g_stack1, sizeof(g_stack1), 10);
    kernel::ThreadId t2 = createThread("t2", g_stack2, sizeof(g_stack2), 10);

    // t1 is the receiver: add to scheduler and make running
    makeRunning(t1);
    m_scheduler.addThread(t2);

    // t1 tries to receive from an empty mailbox -- should block
    kernel::Message msg{};
    kernel::messageReceive(&msg);

    kernel::ThreadControlBlock *tcb1 = kernel::threadGetTcb(t1);
    EXPECT_EQ(tcb1->state, kernel::ThreadState::Blocked);

    kernel::ThreadMailbox *box = kernel::ipcGetMailbox(t1);
    EXPECT_EQ(box->blockReason, kernel::IpcBlockReason::Receive);
}

TEST_F(IpcTest, Receive_UnblocksWhenMessageSent)
{
    kernel::ThreadId t1 = createThread("receiver", g_stack1, sizeof(g_stack1), 10);
    kernel::ThreadId t2 = createThread("sender", g_stack2, sizeof(g_stack2), 10);

    // t1 does receive, blocks
    makeRunning(t1);
    m_scheduler.addThread(t2);
    kernel::Message recvMsg{};
    kernel::messageReceive(&recvMsg);

    // Now t2 is running (scheduler switched), send to t1
    forceCurrent(t2);
    kernel::Message sendMsg = makeRequest(0xBEEF, 42);
    kernel::messageTrySend(t1, sendMsg);

    // t1 should be unblocked
    kernel::ThreadControlBlock *tcb1 = kernel::threadGetTcb(t1);
    EXPECT_EQ(tcb1->state, kernel::ThreadState::Ready);
}

TEST_F(IpcTest, Receive_RejectsFromIsr)
{
    kernel::ThreadId t1 = createThread("t1", g_stack1, sizeof(g_stack1));
    makeRunning(t1);

    test::g_isrContext = true;
    kernel::Message msg{};
    EXPECT_EQ(kernel::messageReceive(&msg), kernel::kIpcErrIsr);
}

// ---- Send/Reply RPC ----

TEST_F(IpcTest, SendReply_BasicRoundTrip)
{
    kernel::ThreadId server = createThread("server", g_stack1, sizeof(g_stack1), 10);
    kernel::ThreadId client = createThread("client", g_stack2, sizeof(g_stack2), 10);

    // Client sends to server, blocks waiting for reply
    forceCurrent(client);
    m_scheduler.addThread(server);
    m_scheduler.addThread(client);

    std::uint32_t sendPayload = 99;
    kernel::Message request = makeRequest(0x1234, 1, &sendPayload, sizeof(sendPayload));
    kernel::Message reply{};
    kernel::messageSend(server, request, &reply);

    // Client should be blocked waiting for reply
    kernel::ThreadControlBlock *clientTcb = kernel::threadGetTcb(client);
    EXPECT_EQ(clientTcb->state, kernel::ThreadState::Blocked);
    kernel::ThreadMailbox *clientBox = kernel::ipcGetMailbox(client);
    EXPECT_EQ(clientBox->blockReason, kernel::IpcBlockReason::Reply);

    // Server receives the message
    forceCurrent(server);
    kernel::Message serverMsg{};
    std::int32_t rc = kernel::messageTryReceive(&serverMsg);
    EXPECT_EQ(rc, kernel::kIpcOk);
    EXPECT_EQ(serverMsg.sender, client);
    EXPECT_EQ(serverMsg.methodId, 1u);

    std::uint32_t receivedVal = 0;
    std::memcpy(&receivedVal, serverMsg.payload, sizeof(receivedVal));
    EXPECT_EQ(receivedVal, 99u);

    // Server sends reply
    kernel::Message replyMsg{};
    replyMsg.type = static_cast<std::uint8_t>(kernel::MessageType::Reply);
    replyMsg.status = 0;
    std::uint32_t replyPayload = 200;
    std::memcpy(replyMsg.payload, &replyPayload, sizeof(replyPayload));
    replyMsg.payloadSize = sizeof(replyPayload);

    rc = kernel::messageReply(client, replyMsg);
    EXPECT_EQ(rc, kernel::kIpcOk);

    // Client should be unblocked
    EXPECT_EQ(clientTcb->state, kernel::ThreadState::Ready);
    EXPECT_EQ(clientBox->blockReason, kernel::IpcBlockReason::None);

    // Client's reply should be filled
    std::uint32_t replyVal = 0;
    std::memcpy(&replyVal, reply.payload, sizeof(replyVal));
    EXPECT_EQ(replyVal, 200u);
    EXPECT_EQ(reply.status, 0);
}

TEST_F(IpcTest, Send_RejectsFromIsr)
{
    kernel::ThreadId t1 = createThread("t1", g_stack1, sizeof(g_stack1));
    kernel::ThreadId t2 = createThread("t2", g_stack2, sizeof(g_stack2));
    makeRunning(t1);
    m_scheduler.addThread(t2);

    test::g_isrContext = true;
    kernel::Message msg = makeRequest(0x1234, 1);
    kernel::Message reply{};
    EXPECT_EQ(kernel::messageSend(t2, msg, &reply), kernel::kIpcErrIsr);
}

TEST_F(IpcTest, Send_InvalidDest_ReturnsNoThread)
{
    kernel::ThreadId t1 = createThread("t1", g_stack1, sizeof(g_stack1));
    makeRunning(t1);

    kernel::Message msg = makeRequest(0x1234, 1);
    kernel::Message reply{};
    EXPECT_EQ(kernel::messageSend(kernel::kInvalidThreadId, msg, &reply),
              kernel::kIpcErrNoThread);
}

TEST_F(IpcTest, Send_NullReply_ReturnsInvalid)
{
    kernel::ThreadId t1 = createThread("t1", g_stack1, sizeof(g_stack1), 10);
    kernel::ThreadId t2 = createThread("t2", g_stack2, sizeof(g_stack2), 10);
    makeRunning(t1);
    m_scheduler.addThread(t2);

    kernel::Message msg = makeRequest(0x1234, 1);
    EXPECT_EQ(kernel::messageSend(t2, msg, nullptr), kernel::kIpcErrInvalid);
}

TEST_F(IpcTest, Reply_InvalidDest_ReturnsNoThread)
{
    kernel::Message reply{};
    EXPECT_EQ(kernel::messageReply(kernel::kInvalidThreadId, reply),
              kernel::kIpcErrNoThread);
}

TEST_F(IpcTest, Reply_NotWaiting_ReturnsInvalid)
{
    kernel::ThreadId t1 = createThread("t1", g_stack1, sizeof(g_stack1));
    makeRunning(t1);

    // t1 is not blocked waiting for a reply
    kernel::Message reply{};
    EXPECT_EQ(kernel::messageReply(t1, reply), kernel::kIpcErrInvalid);
}

// ---- Async notifications ----

TEST_F(IpcTest, Notify_SetsBits)
{
    kernel::ThreadId t1 = createThread("t1", g_stack1, sizeof(g_stack1));
    makeRunning(t1);

    EXPECT_EQ(kernel::messageNotify(t1, 0x01), kernel::kIpcOk);
    EXPECT_EQ(kernel::messageNotify(t1, 0x04), kernel::kIpcOk);

    kernel::ThreadMailbox *box = kernel::ipcGetMailbox(t1);
    EXPECT_EQ(box->notifyBits, 0x05u);
}

TEST_F(IpcTest, Notify_CheckClearsBits)
{
    kernel::ThreadId t1 = createThread("t1", g_stack1, sizeof(g_stack1));
    makeRunning(t1);

    kernel::messageNotify(t1, 0x0F);

    std::uint32_t bits = kernel::messageCheckNotify();
    EXPECT_EQ(bits, 0x0Fu);

    // Check again -- should be cleared
    bits = kernel::messageCheckNotify();
    EXPECT_EQ(bits, 0u);
}

TEST_F(IpcTest, Notify_IsrSafe)
{
    kernel::ThreadId t1 = createThread("t1", g_stack1, sizeof(g_stack1));
    makeRunning(t1);

    test::g_isrContext = true;
    EXPECT_EQ(kernel::messageNotify(t1, 0x80), kernel::kIpcOk);

    kernel::ThreadMailbox *box = kernel::ipcGetMailbox(t1);
    EXPECT_EQ(box->notifyBits, 0x80u);
}

TEST_F(IpcTest, Notify_InvalidDest_ReturnsNoThread)
{
    EXPECT_EQ(kernel::messageNotify(kernel::kInvalidThreadId, 0x01),
              kernel::kIpcErrNoThread);
}

TEST_F(IpcTest, Notify_ZeroBits_ReturnsInvalid)
{
    kernel::ThreadId t1 = createThread("t1", g_stack1, sizeof(g_stack1));
    makeRunning(t1);
    EXPECT_EQ(kernel::messageNotify(t1, 0), kernel::kIpcErrInvalid);
}

TEST_F(IpcTest, Notify_AccumulatesBits)
{
    kernel::ThreadId t1 = createThread("t1", g_stack1, sizeof(g_stack1));
    makeRunning(t1);

    kernel::messageNotify(t1, 0x01);
    kernel::messageNotify(t1, 0x02);
    kernel::messageNotify(t1, 0x01);  // Already set

    kernel::ThreadMailbox *box = kernel::ipcGetMailbox(t1);
    EXPECT_EQ(box->notifyBits, 0x03u);
}

// ---- Message struct ----

TEST_F(IpcTest, MessageStruct_Size64Bytes)
{
    EXPECT_EQ(sizeof(kernel::Message), 64u);
}

// ---- Send with full mailbox ----

TEST_F(IpcTest, TrySend_MultipleReceive_DrainAll)
{
    // Send multiple messages, receive all of them, verify count returns to 0
    kernel::ThreadId t1 = createThread("sender", g_stack1, sizeof(g_stack1), 10);
    kernel::ThreadId t2 = createThread("receiver", g_stack2, sizeof(g_stack2), 10);
    makeRunning(t1);
    m_scheduler.addThread(t2);

    for (std::uint8_t i = 0; i < kernel::kMailboxDepth; ++i)
    {
        kernel::Message msg = makeRequest(0x1234, i);
        EXPECT_EQ(kernel::messageTrySend(t2, msg), kernel::kIpcOk);
    }

    kernel::ThreadMailbox *box = kernel::ipcGetMailbox(t2);
    EXPECT_EQ(box->count, kernel::kMailboxDepth);

    forceCurrent(t2);
    for (std::uint8_t i = 0; i < kernel::kMailboxDepth; ++i)
    {
        kernel::Message msg{};
        EXPECT_EQ(kernel::messageTryReceive(&msg), kernel::kIpcOk);
    }
    EXPECT_EQ(box->count, 0u);
}

// ---- TrySend wakes blocked receiver ----

TEST_F(IpcTest, TrySend_WakesBlockedReceiver)
{
    kernel::ThreadId receiver = createThread("receiver", g_stack1, sizeof(g_stack1), 10);
    kernel::ThreadId sender = createThread("sender", g_stack2, sizeof(g_stack2), 10);

    // receiver blocks on receive
    makeRunning(receiver);
    m_scheduler.addThread(sender);
    kernel::Message recvMsg{};
    kernel::messageReceive(&recvMsg);

    kernel::ThreadControlBlock *recvTcb = kernel::threadGetTcb(receiver);
    EXPECT_EQ(recvTcb->state, kernel::ThreadState::Blocked);

    // sender sends -- should wake receiver
    forceCurrent(sender);
    kernel::Message msg = makeRequest(0xBEEF, 5);
    kernel::messageTrySend(receiver, msg);

    EXPECT_EQ(recvTcb->state, kernel::ThreadState::Ready);
}

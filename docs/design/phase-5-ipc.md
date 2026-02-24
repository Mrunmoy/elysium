# Phase 5: IPC / Message Passing

## Overview

Phase 5 adds inter-thread communication to ms-os following the Minix
send/receive/reply model. Each thread receives a statically allocated
mailbox with a 4-slot ring buffer of 64-byte messages. An IDL code
generator (ipcgen) produces typed client stubs and server dispatch loops
from service definitions, enabling structured RPC over the kernel IPC
primitives.

---

## Design

The IPC subsystem is built on three layers: a per-thread mailbox for
message storage, blocking/non-blocking kernel primitives for message
transfer, and an optional code generator for typed RPC interfaces.

```
 +-------------------+          +-------------------+
 |   Client Thread   |          |   Server Thread   |
 | EchoClient::Ping()|         | echoServerThread() |
 +--------+----------+          +--------+----------+
          |                              |
          | messageSend(dest, req, &rpl) | messageReceive(&msg)
          |                              |
 +--------v------------------------------v----------+
 |              Kernel IPC Layer (Ipc.cpp)           |
 |                                                   |
 |  s_mailboxes[kMaxThreads]                         |
 |  +----------+  +----------+       +----------+   |
 |  | Mailbox 0|  | Mailbox 1| . . . | Mailbox 7|   |
 |  | slots[4] |  | slots[4] |       | slots[4] |   |
 |  | head/tail|  | head/tail|       | head/tail|   |
 |  | notifyBt |  | notifyBt |       | notifyBt |   |
 |  | waitQueues  | waitQueues        | waitQueues   |
 |  +----------+  +----------+       +----------+   |
 |                                                   |
 |  Blocking: enterCritical -> waitQueueInsert ->    |
 |            blockCurrentThread -> switchContext     |
 +---------------------------------------------------+
```

### Message Structure (64 bytes)

```
  0      1      2      4          8         12     14    16              64
 +------+------+------+----------+---------+------+-----+---------------+
 |sender| type |method|serviceId | status  |pySz  | pad |  payload[48]  |
 |  1B  |  1B  |  2B  |   4B     |   4B    | 2B   | 2B  |    48B        |
 +------+------+------+----------+---------+------+-----+---------------+
  header (16 bytes)                                       payload (48 bytes)
```

- `sender` is stamped by the kernel on enqueue, not by the caller
- `serviceId` is an FNV-1a 32-bit hash of the service name
- `methodId` identifies the specific RPC method within the service
- `status` carries the return code in reply messages
- `payloadSize` indicates the number of valid bytes in `payload[]`

---

## Kernel API (kernel/inc/kernel/Ipc.h)

### Constants

```cpp
static constexpr std::uint8_t kMaxPayloadSize = 48;
static constexpr std::uint8_t kMailboxDepth   = 4;

static constexpr std::int32_t kIpcOk          =  0;
static constexpr std::int32_t kIpcErrInvalid  = -1;  // Invalid argument
static constexpr std::int32_t kIpcErrFull     = -2;  // Mailbox full (non-blocking)
static constexpr std::int32_t kIpcErrEmpty    = -3;  // Mailbox empty (non-blocking)
static constexpr std::int32_t kIpcErrNoThread = -4;  // Destination thread invalid
static constexpr std::int32_t kIpcErrIsr      = -5;  // Cannot block from ISR
static constexpr std::int32_t kIpcErrMethod   = -6;  // Unknown method ID
```

### Synchronous RPC (Send / Receive / Reply)

The three-phase Minix-style protocol:

1. **Client** calls `messageSend()` -- blocks until the message is
   delivered, then blocks again waiting for the server's reply.
2. **Server** calls `messageReceive()` -- blocks until a message arrives.
3. **Server** calls `messageReply()` -- copies the reply into the
   client's reply slot and unblocks the client.

```cpp
// Client: send request, block until reply
std::int32_t messageSend(ThreadId dest, const Message &msg, Message *reply);

// Server: block until a message arrives
std::int32_t messageReceive(Message *msg);

// Server: send reply, unblock the client
std::int32_t messageReply(ThreadId dest, const Message &reply);
```

### Non-Blocking Variants

```cpp
// Enqueue message or return kIpcErrFull. Does not wait for reply.
std::int32_t messageTrySend(ThreadId dest, const Message &msg);

// Dequeue message or return kIpcErrEmpty.
std::int32_t messageTryReceive(Message *msg);
```

`messageTrySend` is used by the IDL-generated notification sender and by
application threads that do not need a reply (fire-and-forget).

### Async Notifications (ISR-safe)

Lightweight bitmask-based notifications with no message copy:

```cpp
// Set bits atomically in dest's notifyBits. Non-blocking, ISR-safe.
std::int32_t messageNotify(ThreadId dest, std::uint32_t bits);

// Read and clear the current thread's notifyBits.
std::uint32_t messageCheckNotify();
```

Notifications use a single `uint32_t` per thread (32 independent
notification channels). The OR operation is interrupt-safe because it
executes inside `enterCritical()` / `exitCritical()` and involves no
blocking or context switch.

---

## Data Structures

### Message

```cpp
struct Message
{
    ThreadId sender;                  //  1B
    std::uint8_t type;                //  1B (MessageType enum)
    std::uint16_t methodId;           //  2B
    std::uint32_t serviceId;          //  4B (FNV-1a hash)
    std::int32_t status;              //  4B
    std::uint16_t payloadSize;        //  2B
    std::uint8_t reserved[2];         //  2B padding
    std::uint8_t payload[48];         // 48B
};
static_assert(sizeof(Message) == 64, "Message must be exactly 64 bytes");
```

### MessageType

```cpp
enum class MessageType : std::uint8_t
{
    Request  = 1,
    Reply    = 2,
    Notify   = 3,
    OneWay   = 4,
};
```

### ThreadMailbox

One mailbox per thread, stored in a static array `s_mailboxes[kMaxThreads]`
inside `Ipc.cpp`. The IPC subsystem does not modify the TCB -- all IPC
state is kept separately.

```cpp
struct ThreadMailbox
{
    Message slots[kMailboxDepth];     // 4-slot ring buffer
    std::uint8_t head;                // Read index
    std::uint8_t tail;                // Write index
    std::uint8_t count;               // Pending messages
    std::uint8_t pad;
    std::uint32_t notifyBits;         // Async notification bitmask
    ThreadId senderWaitHead;          // Wait queue: threads blocked on send
    ThreadId receiverWaitHead;        // Wait queue: thread blocked on receive
    std::uint8_t senderWaitCount;
    std::uint8_t receiverWaitCount;
    IpcBlockReason blockReason;       // Why the owning thread is blocked
    Message *replySlot;               // Where to write reply for send-and-wait
};
```

### IpcBlockReason

```cpp
enum class IpcBlockReason : std::uint8_t
{
    None    = 0,
    Send    = 1,  // Dest mailbox full, waiting for space
    Receive = 2,  // Own mailbox empty, waiting for message
    Reply   = 3,  // Sent message, waiting for server reply
};
```

---

## Implementation Details

### Blocking Pattern

IPC blocking follows the same pattern used by Mutex and Semaphore:

```
enterCritical()
  waitQueueInsert(head, currentId)
  scheduler.blockCurrentThread()
  doSwitchContext()
exitCritical()
triggerContextSwitch()

-- thread resumes here when unblocked --
```

Wait queues are priority-sorted singly-linked lists (via `WaitQueue.h`).
Each mailbox maintains two wait queues:
- `senderWaitHead` -- threads blocked because the mailbox is full
- `receiverWaitHead` -- the thread blocked waiting for an incoming message

### messageSend() Flow

```
1. Validate dest thread exists and is not Inactive
2. enterCritical()
3. If dest mailbox full:
   a. Set blockReason = Send
   b. Insert self into dest's senderWaitHead queue
   c. Block + switchContext
   d. (Resume when space available) -> re-enter critical
4. Enqueue message into dest mailbox, stamp sender field
5. If dest is blocked on Receive:
   a. Remove dest from its receiverWaitHead queue
   b. Unblock dest
6. Set blockReason = Reply, store replySlot pointer
7. Block + switchContext
8. (Resume when server calls messageReply())
9. Return kIpcOk
```

### messageReceive() Flow

```
1. enterCritical()
2. If messages pending:
   a. Dequeue from own mailbox
   b. If any sender blocked on senderWaitHead, unblock one
   c. exitCritical(), return kIpcOk
3. If no messages:
   a. Set blockReason = Receive
   b. Insert self into own receiverWaitHead queue
   c. Block + switchContext
   d. (Resume when message arrives) -> re-enter critical
   e. Dequeue message
   f. Unblock any blocked sender
   g. exitCritical(), return kIpcOk
```

### messageReply() Flow

```
1. Validate dest thread exists
2. enterCritical()
3. Verify dest's blockReason == Reply and replySlot != nullptr
4. Copy reply into dest's replySlot
5. Clear replySlot and blockReason
6. Unblock dest
7. If preemption needed, switchContext + triggerContextSwitch
8. exitCritical(), return kIpcOk
```

### Ring Buffer Internals

The mailbox uses a circular buffer with modular arithmetic:

```cpp
// Enqueue
slot = slots[tail];
tail = (tail + 1) % kMailboxDepth;
++count;

// Dequeue
msg = slots[head];
head = (head + 1) % kMailboxDepth;
--count;
```

The `count` field disambiguates full vs. empty (both have `head == tail`).

### ISR Restrictions

`messageSend()` and `messageReceive()` check `arch::inIsrContext()` and
return `kIpcErrIsr` if called from interrupt context, since they may block.
`messageTrySend()` and `messageNotify()` are safe from ISR context because
they never block.

---

## IDL Code Generator (ipcgen)

### Overview

The `tools/ipcgen/` module is a Python code generator that reads `.idl`
service definitions and emits embedded-friendly C++ server and client
source files targeting the kernel IPC API.

The lexer, parser, and type system are reused from the ms-ipc library
(the desktop IPC framework). The `embedded_emitter.py` is new -- it
generates code that calls `messageSend/Receive/Reply` directly instead
of the desktop shared-memory transport.

### Pipeline

```
  Echo.idl
     |
     v
  Lexer (lexer.py)      -- tokenize into KEYWORD, IDENT, NUMBER, SYMBOL, ATTR
     |
     v
  Parser (parser.py)     -- recursive-descent, builds IdlFile AST
     |
     v
  Emitter (embedded_emitter.py) -- generate up to 5 C++ files
     |
     +---> EchoTypes.h       (only if enums/structs defined)
     +---> EchoServer.h      (server base class with pure virtual handlers)
     +---> EchoServer.cpp    (run() loop + dispatch switch + notification senders)
     +---> EchoClient.h      (client class with typed RPC stubs)
     +---> EchoClient.cpp    (marshal -> messageSend -> unmarshal)
```

### IDL Syntax

```idl
// Optional enum/struct definitions
enum DeviceType { Sensor = 0, Actuator = 1 };

struct DeviceInfo {
    uint32 id;
    DeviceType type;
    uint8[6] serial;
};

// Service methods (synchronous RPC)
service Echo
{
    [method=1]
    int Ping([in] uint32 value, [out] uint32 result);

    [method=2]
    int Add([in] uint32 a, [in] uint32 b, [out] uint32 sum);

    [method=3]
    int GetCount([out] uint32 count);
};

// Notifications (async, fire-and-forget)
notifications Echo
{
    [notify=1]
    void CountChanged([in] uint32 newCount);
};
```

**Built-in types:** `uint8`, `uint16`, `uint32`, `uint64`, `int8`, `int16`,
`int32`, `int64`, `float32`, `float64`, `bool`, `string[N]`.

**Attributes:**
- `[method=N]` -- assigns a numeric method ID for the dispatch switch
- `[notify=N]` -- assigns a numeric notification ID
- `[in]` / `[out]` -- parameter direction

**Service IDs** are computed as FNV-1a 32-bit hashes of the service name
(e.g., `fnv1a_32("Echo") = 0x3b7d6ba4`).

### Generated Server

The generated server is an abstract base class. The implementor subclasses
it and overrides pure virtual `handleXxx()` methods:

```cpp
class EchoServer
{
public:
    static constexpr std::uint32_t kServiceId = 0x3b7d6ba4u;

    void run();   // Blocking receive loop

    std::int32_t notifyCountChanged(kernel::ThreadId dest, uint32_t newCount);

    virtual ~EchoServer() = default;

protected:
    virtual std::int32_t handlePing(uint32_t value, uint32_t *result) = 0;
    virtual std::int32_t handleAdd(uint32_t a, uint32_t b, uint32_t *sum) = 0;
    virtual std::int32_t handleGetCount(uint32_t *count) = 0;

private:
    void dispatch(const kernel::Message &request);
};
```

The `run()` method is an infinite loop:

```cpp
void EchoServer::run()
{
    kernel::Message msg;
    while (true)
    {
        std::int32_t rc = kernel::messageReceive(&msg);
        if (rc == kernel::kIpcOk)
            dispatch(msg);
    }
}
```

The `dispatch()` method:
1. Prepares a reply message (zeroed, type=Reply, serviceId, methodId)
2. Switches on `request.methodId`
3. Unmarshals `[in]` params from `request.payload` via `std::memcpy`
4. Calls the virtual handler
5. Marshals `[out]` params into `reply.payload`
6. Calls `kernel::messageReply(request.sender, reply)`
7. Default case returns `kIpcErrMethod`

### Generated Client

The client holds a `m_serverTid` and provides typed method stubs:

```cpp
class EchoClient
{
public:
    explicit EchoClient(kernel::ThreadId serverTid);

    std::int32_t Ping(uint32_t value, uint32_t *result);
    std::int32_t Add(uint32_t a, uint32_t b, uint32_t *sum);
    std::int32_t GetCount(uint32_t *count);

private:
    kernel::ThreadId m_serverTid;
};
```

Each stub:
1. Marshals `[in]` params into `request.payload` via `std::memcpy`
2. Calls `kernel::messageSend(m_serverTid, request, &reply)`
3. Checks transport error (`rc != kIpcOk` -> early return)
4. Unmarshals `[out]` params from `reply.payload`
5. Returns `reply.status`

### Notifications

Notification methods are generated on the server class because they are
sent *from* the server *to* a client thread:

- **With payload:** Uses `messageTrySend()` (non-blocking, carries data
  in the message payload). The notification is typed as `MessageType::Notify`.
- **Without payload (parameterless):** Uses `messageNotify()` (bitmask OR,
  ISR-safe). The bit position is `1u << notifyId`.

### Payload Overflow Protection

Every generated method includes compile-time `static_assert` checks:

```cpp
static_assert(sizeof(uint32_t) + sizeof(uint32_t) <= kernel::kMaxPayloadSize,
    "Add: [in] params exceed payload limit");
static_assert(sizeof(uint32_t) <= kernel::kMaxPayloadSize,
    "Add: [out] params exceed payload limit");
```

If a method's parameters exceed the 48-byte payload limit, the build
fails with a clear error message.

### CLI Usage

```bash
python3 -m tools.ipcgen services/echo/Echo.idl --outdir services/echo/gen/
```

Generates:
```
  wrote services/echo/gen/EchoServer.h
  wrote services/echo/gen/EchoServer.cpp
  wrote services/echo/gen/EchoClient.h
  wrote services/echo/gen/EchoClient.cpp

Generated 4 files for service 'Echo' (serviceId=0x3b7d6ba4)
```

---

## Example: Echo Service

The `services/echo/Echo.idl` defines a demonstration service with three
RPC methods (Ping, Add, GetCount) and one notification (CountChanged).

The `app/ipc-demo/main.cpp` application creates:
- A privileged server thread (priority 8) that implements the echo logic
  inline (no generated code dependency for simplicity)
- An unprivileged client thread (priority 10) that sends requests via
  `kernel::user::messageSend()` (SVC wrapper)
- The server prints each request/response to UART

```
srv: ping(0)
srv: add(1,2)=3
srv: count=3
srv: ping(10)
srv: add(2,3)=5
srv: count=6
```

---

## Memory Budget

| Component | Size |
|-----------|------|
| `s_mailboxes[8]` (8 threads * 276 bytes/mailbox) | ~2.2 KB |
| `Ipc.cpp` code (.text) | ~1.2 KB |
| Per generated service (server + client .text) | ~0.5 KB |
| **Total (kernel IPC, 8 threads)** | **~3.4 KB** |

Each `ThreadMailbox` contains 4 * 64 = 256 bytes of message slots plus
~20 bytes of metadata. No dynamic allocation is used.

---

## Design Decisions

| Decision | Rationale |
|----------|-----------|
| Per-thread mailbox (not channel-based) | Minix model: address by ThreadId, simple, no dynamic allocation |
| 4-slot ring buffer | Balances memory (256B/thread) vs. ability to absorb bursts |
| 64-byte messages (16B header + 48B payload) | Fits small RPC args inline; cache-line friendly |
| IPC state in separate array (no TCB changes) | Decouples IPC from scheduler; no TCB layout version bump |
| FNV-1a 32-bit service ID | Fast, deterministic, no string comparison at runtime |
| `static_assert` for payload overflow | Compile-time safety; no runtime size checks needed |
| Notification bitmask (not message) | ISR-safe, zero-copy, 32 independent channels per thread |
| Priority-sorted wait queues | Higher-priority senders/receivers are unblocked first |
| `messageTrySend` for notifications with payload | Avoids blocking in notification senders (fire-and-forget) |
| Reuse ms-ipc lexer/parser | Consistent IDL syntax; avoids maintaining two parsers |

---

## Test Coverage

### Kernel IPC Tests (C++)

29 tests in `test/kernel/IpcTest.cpp`:

- **Init/Reset:** Mailboxes start empty, invalid ID returns null
- **Non-blocking send:** Succeeds to valid thread, stamps sender, fails
  when full, rejects invalid dest
- **Non-blocking receive:** Succeeds when pending, fails when empty,
  rejects null msg pointer
- **Ring buffer:** Wraparound after fill/drain cycles, FIFO ordering
- **Blocking receive:** Blocks when empty, unblocks when message sent,
  rejects from ISR context
- **Send/Reply RPC:** Full round-trip (send -> receive -> reply), client
  state transitions (Blocked/Reply -> Ready), payload integrity
- **Error cases:** Send from ISR, invalid dest, null reply pointer,
  reply to non-waiting thread
- **Notifications:** Set bits, check-and-clear, ISR-safe, invalid dest,
  zero bits rejected, bit accumulation
- **Message struct:** `sizeof(Message) == 64` enforced

### IDL Code Generator Tests (Python)

40 tests in `tools/ipcgen/test/test_embedded_emitter.py`:

- **Types header:** pragma once, namespaces, cstdint include, enum values,
  struct fields, no std::array/vector
- **Server header:** Class name, serviceId constant, method enum, notify
  enum, run() method, pure virtual handlers, includes
- **Server implementation:** run() calls messageReceive, dispatch switch
  cases, messageReply call, static_assert, default kIpcErrMethod,
  handler invocations, memcpy marshaling
- **Client header:** Class name, constructor with ThreadId, method stubs,
  private m_serverTid
- **Client implementation:** messageSend call, reply.status return,
  transport error check, static_assert, memcpy marshaling,
  MessageType::Request
- **User-defined types:** Types header included in server/client,
  handler uses custom types
- **End-to-end:** All generated files are non-empty, auto-generated
  comment present

---

## Files

| File | Purpose |
|------|---------|
| `kernel/inc/kernel/Ipc.h` | Public IPC API: Message, ThreadMailbox, function declarations |
| `kernel/src/core/Ipc.cpp` | Implementation: mailbox operations, blocking, notifications (~415 lines) |
| `kernel/src/core/WaitQueue.h` | Priority-sorted wait queue (shared with Mutex/Semaphore) |
| `tools/ipcgen/__main__.py` | CLI entry point for code generator |
| `tools/ipcgen/lexer.py` | IDL tokenizer (keywords, identifiers, attributes) |
| `tools/ipcgen/parser.py` | Recursive-descent parser producing IdlFile AST |
| `tools/ipcgen/types.py` | IDL-to-C++ type map, FNV-1a hash function |
| `tools/ipcgen/embedded_emitter.py` | C++ code emitter for kernel IPC target (~575 lines) |
| `tools/ipcgen/test/conftest.py` | Test fixtures (Echo + DeviceManager IDL) |
| `tools/ipcgen/test/test_embedded_emitter.py` | 40 pytest tests for generated code |
| `services/echo/Echo.idl` | Example service definition (Ping, Add, GetCount, CountChanged) |
| `services/echo/gen/EchoServer.h` | Generated server header |
| `services/echo/gen/EchoServer.cpp` | Generated server dispatch loop |
| `services/echo/gen/EchoClient.h` | Generated client header |
| `services/echo/gen/EchoClient.cpp` | Generated client RPC stubs |
| `app/ipc-demo/main.cpp` | Demo application: echo server + client threads |
| `test/kernel/IpcTest.cpp` | 29 Google Test cases for kernel IPC |

// Global operator new/delete routing to kernel heap + _sbrk stub.
// Cross-compile only -- not linked in host tests (host uses system allocator).

#include "kernel/Heap.h"

#include <cstddef>

// Stub _sbrk to prevent newlib from using its own heap
extern "C" void *_sbrk(ptrdiff_t)
{
    return reinterpret_cast<void *>(-1);
}

void *operator new(std::size_t size)
{
    return kernel::heapAlloc(static_cast<std::uint32_t>(size));
}

void *operator new[](std::size_t size)
{
    return kernel::heapAlloc(static_cast<std::uint32_t>(size));
}

void operator delete(void *ptr) noexcept
{
    kernel::heapFree(ptr);
}

void operator delete[](void *ptr) noexcept
{
    kernel::heapFree(ptr);
}

void operator delete(void *ptr, std::size_t) noexcept
{
    kernel::heapFree(ptr);
}

void operator delete[](void *ptr, std::size_t) noexcept
{
    kernel::heapFree(ptr);
}

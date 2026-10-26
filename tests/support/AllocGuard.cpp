// SPDX-License-Identifier: AGPL-3.0-or-later
// Replacement of the global allocation functions for the test executables: every operator new on a thread bumps that
// thread's counter, which AllocGuard reads (TEST_PLAN T-RT-01: no allocation on the audio path after prepare).
#include "support/AllocGuard.h"

#include <cstdlib>
#include <new>

#if defined(_WIN32)
#include <malloc.h>
#endif

namespace
{
thread_local std::size_t newCalls = 0;
thread_local std::size_t freeCalls = 0; // non-null operator delete calls

void release(void* p) noexcept
{
    if (p != nullptr)
        ++freeCalls;
    std::free(p);
}

void* allocate(std::size_t size) noexcept
{
    ++newCalls;
    return std::malloc(size == 0 ? 1 : size);
}

void* allocateAligned(std::size_t size, std::align_val_t alignment) noexcept
{
    ++newCalls;
    const auto align = static_cast<std::size_t>(alignment);
#if defined(_WIN32)
    return _aligned_malloc(size == 0 ? 1 : size, align);
#else
    void* p = nullptr;
    return posix_memalign(&p, align < sizeof(void*) ? sizeof(void*) : align, size == 0 ? 1 : size) == 0 ? p : nullptr;
#endif
}

void freeAligned(void* p) noexcept
{
    if (p != nullptr)
        ++freeCalls;
#if defined(_WIN32)
    _aligned_free(p);
#else
    std::free(p);
#endif
}

void* orThrow(void* p)
{
    if (p == nullptr)
        throw std::bad_alloc();
    return p;
}
} // namespace

namespace subgen::test
{
AllocGuard::AllocGuard()
    : start(newCalls),
      startFreed(freeCalls)
{
}

std::size_t AllocGuard::count() const
{
    return newCalls - start;
}

std::ptrdiff_t AllocGuard::live() const
{
    return static_cast<std::ptrdiff_t>(newCalls - start) - static_cast<std::ptrdiff_t>(freeCalls - startFreed);
}
} // namespace subgen::test

void* operator new(std::size_t size)
{
    return orThrow(allocate(size));
}
void* operator new[](std::size_t size)
{
    return orThrow(allocate(size));
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    return allocate(size);
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    return allocate(size);
}
void* operator new(std::size_t size, std::align_val_t alignment)
{
    return orThrow(allocateAligned(size, alignment));
}
void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return orThrow(allocateAligned(size, alignment));
}
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return allocateAligned(size, alignment);
}
void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return allocateAligned(size, alignment);
}

void operator delete(void* p) noexcept
{
    release(p);
}
void operator delete[](void* p) noexcept
{
    release(p);
}
void operator delete(void* p, std::size_t) noexcept
{
    release(p);
}
void operator delete[](void* p, std::size_t) noexcept
{
    release(p);
}
void operator delete(void* p, const std::nothrow_t&) noexcept
{
    release(p);
}
void operator delete[](void* p, const std::nothrow_t&) noexcept
{
    release(p);
}
void operator delete(void* p, std::align_val_t) noexcept
{
    freeAligned(p);
}
void operator delete[](void* p, std::align_val_t) noexcept
{
    freeAligned(p);
}
void operator delete(void* p, std::size_t, std::align_val_t) noexcept
{
    freeAligned(p);
}
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept
{
    freeAligned(p);
}
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept
{
    freeAligned(p);
}
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept
{
    freeAligned(p);
}

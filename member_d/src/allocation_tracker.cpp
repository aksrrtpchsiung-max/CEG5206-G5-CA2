// Tracks requested C++ allocation bytes, including container capacity and temporary
// allocations. Runtime malloc/mmap allocations are instead reflected in process RSS.
#include "allocation_tracker.hpp"
#include <atomic>
#include <cstdlib>
#include <new>
#include <sys/resource.h>
#include <cstddef>
#include <cstdint>
namespace {
std::atomic<std::uint64_t> current{0}, high{0};
struct Header { void* base; std::size_t size; };
void* allocate(std::size_t size, std::size_t alignment) {
    if (!size) size = 1;
    if (alignment < alignof(std::max_align_t)) alignment = alignof(std::max_align_t);
    if (size > SIZE_MAX - alignment - sizeof(Header)) throw std::bad_alloc();
    void* base = std::malloc(size + alignment + sizeof(Header));
    if (!base) throw std::bad_alloc();
    auto address = (reinterpret_cast<std::uintptr_t>(base) + sizeof(Header) + alignment - 1) & ~(alignment - 1);
    auto* h = reinterpret_cast<Header*>(address) - 1;
    h->base = base; h->size = size;
    const auto count = current.fetch_add(size) + size;
    auto old = high.load();
    while (old < count && !high.compare_exchange_weak(old, count)) {}
    return reinterpret_cast<void*>(address);
}
void release(void* p) noexcept {
    if (!p) return;
    auto* h = reinterpret_cast<Header*>(p) - 1;
    current.fetch_sub(h->size);
    std::free(h->base);
}
}
void* operator new(std::size_t s) { return allocate(s, alignof(std::max_align_t)); }
void* operator new[](std::size_t s) { return allocate(s, alignof(std::max_align_t)); }
void operator delete(void* p) noexcept { release(p); }
void operator delete[](void* p) noexcept { release(p); }
void operator delete(void* p, std::size_t) noexcept { release(p); }
void operator delete[](void* p, std::size_t) noexcept { release(p); }
void* operator new(std::size_t s, std::align_val_t a) { return allocate(s, static_cast<std::size_t>(a)); }
void* operator new[](std::size_t s, std::align_val_t a) { return allocate(s, static_cast<std::size_t>(a)); }
void operator delete(void* p, std::align_val_t) noexcept { release(p); }
void operator delete[](void* p, std::align_val_t) noexcept { release(p); }
namespace member_d::allocation {
std::uint64_t live() noexcept { return current.load(); }
std::uint64_t peak() noexcept { return high.load(); }
std::uint64_t rss_peak() noexcept {
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage)) return 0;
#ifdef __APPLE__
    return usage.ru_maxrss;
#else
    return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024;
#endif
}
}

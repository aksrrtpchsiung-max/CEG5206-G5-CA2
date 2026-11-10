#include "allocation_tracker.hpp"
#include <cstdint>
#include <new>
#include <stdexcept>
int main() {
    const auto before = member_d::allocation::live();
    void* p = ::operator new(4096, std::align_val_t(64));
    if (reinterpret_cast<std::uintptr_t>(p) % 64 || member_d::allocation::live() < before + 4096 || member_d::allocation::peak() < before + 4096)
        throw std::runtime_error("aligned allocation accounting failed");
    ::operator delete(p, std::align_val_t(64));
    if (member_d::allocation::live() != before) throw std::runtime_error("allocation release accounting failed");
}

#pragma once
#include <cstdint>
namespace member_d::allocation {
#ifdef D_ALLOCATION_TRACKER
std::uint64_t live() noexcept;
std::uint64_t peak() noexcept;
std::uint64_t rss_peak() noexcept;
#else
inline std::uint64_t live() noexcept { return 0; }
inline std::uint64_t peak() noexcept { return 0; }
inline std::uint64_t rss_peak() noexcept { return 0; }
#endif
}

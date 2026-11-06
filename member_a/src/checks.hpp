#pragma once

#include <ceg5206/contracts.hpp>
#include <cmath>
#include <stdexcept>
#include <type_traits>

namespace ceg5206::a::detail {
using Code = ValidationCode;

inline ValidationResult error(Code code, const char* message,
                              std::uint64_t row = kNoIndex, std::uint64_t index = kNoIndex) {
    return {code, row, index, message};
}

inline void require(ValidationResult result) {
    if (result.ok()) return;
    if (result.code == Code::size_overflow) throw std::overflow_error(result.message);
    throw std::invalid_argument(result.message);
}

template<class T>
constexpr std::uint64_t max_elements() {
    return std::numeric_limits<std::ptrdiff_t>::max() / sizeof(T);
}

inline ValidationResult nodes(std::uint64_t n) {
    if (n > std::numeric_limits<NodeId>::max())
        return error(Code::invalid_shape, "n exceeds the contract's node limit");
    if (n + 1 > max_elements<RowOffset>())
        return error(Code::size_overflow, "row offsets are not addressable");
    return {};
}

template<class T>
ValidationResult storage(Span<T> span) {
    if (span.size > max_elements<T>()) return error(Code::size_overflow, "array size overflow");
    if (!span.size) return {};
    if (!span.data) return error(Code::null_storage, "nonempty array has null data");
    const auto addr = reinterpret_cast<std::uintptr_t>(span.data);
    if (addr % alignof(T)) return error(Code::invalid_shape, "misaligned array");
    if (span.size * sizeof(T) > std::numeric_limits<std::uintptr_t>::max() - addr)
        return error(Code::size_overflow, "address range wraps around");
    return {};
}

template<class T>
ValidationResult dimensions(std::uint64_t n, FeatureCount f) {
    if (auto valid = nodes(n); !valid.ok()) return valid;
    if (f == 0) return error(Code::invalid_shape, "f must be positive");
    if (n > max_elements<T>() / f) return error(Code::size_overflow, "dense byte size overflow");
    return {};
}

template<class View>
ValidationResult dense(View v) {
    using T = std::remove_pointer_t<decltype(v.values.data)>;
    if (auto valid = dimensions<T>(v.n, v.f); !valid.ok()) return valid;
    if (v.values.size != v.n * v.f) return error(Code::invalid_shape, "incorrect dense length");
    return storage(v.values);
}

template<class A, class B>
bool overlap(Span<A> a, Span<B> b) {
    if (!a.size || !b.size) return false;
    const auto x = reinterpret_cast<std::uintptr_t>(a.data);
    const auto y = reinterpret_cast<std::uintptr_t>(b.data);
    return x < y + b.size * sizeof(B) && y < x + a.size * sizeof(A);
}

inline ValidationResult finite_x(DenseConstView x) {
    for (std::size_t i = 0; i < x.values.size; ++i) {
        if (!std::isfinite(x.values.data[i]))
            return error(Code::non_finite_input, "non-finite X", i / x.f, i);
    }
    return {};
}

// Used after validating caller input or sampling canonical edges by construction.
CsrGraph csr_from_sorted_edges(EdgeListView edges);
} // namespace ceg5206::a::detail

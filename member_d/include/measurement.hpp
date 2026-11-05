#pragma once
#include <ceg5206/contracts.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace member_d {
using Clock = std::chrono::steady_clock;
inline std::uint64_t add(std::uint64_t a, std::uint64_t b) {
    if (b > UINT64_MAX - a) throw std::overflow_error("byte count overflow");
    return a + b;
}
inline std::uint64_t mul(std::uint64_t a, std::uint64_t b) {
    if (a && b > UINT64_MAX / a) throw std::overflow_error("array size overflow");
    return a * b;
}
template<class T> std::uint64_t allocated(const std::vector<T>& v) {
    return mul(v.capacity(), sizeof(T));
}
// Linear interpolation at index (N-1)*q; shared with Python summary code.
inline double quantile(std::vector<double> values, double q) {
    if (values.empty() || q < 0 || q > 1) throw std::invalid_argument("invalid quantile");
    for (double x : values)
        if (!std::isfinite(x) || x < 0) throw std::invalid_argument("invalid duration");
    std::sort(values.begin(), values.end());
    const double index = (values.size() - 1) * q;
    const auto lo = static_cast<std::size_t>(index);
    const auto hi = std::min(lo + 1, values.size() - 1);
    return values[lo] + (values[hi] - values[lo]) * (index - lo);
}
struct Samples {
    std::vector<double> seconds;
    double checksum = 0;
};
inline Samples measure(ceg5206::PreparedKernel& kernel, ceg5206::CsrView graph,
                       const std::vector<ceg5206::DenseConstView>& inputs,
                       ceg5206::DenseView y, const ceg5206::RunConfig& cfg,
                       unsigned warmup, unsigned repetitions) {
    if (!kernel.run || inputs.size() != 2 || warmup < 5 || repetitions < 10)
        throw std::invalid_argument("need a real kernel, two inputs, >=5 warmups and >=10 repetitions");
    for (unsigned i = 0; i < warmup; ++i)
        kernel.run(graph, inputs[i % 2], y, cfg, kernel.state.get());
    Samples result;
    result.seconds.reserve(repetitions);
    // Each repetition is one complete call. No clearing outside this interval.
    for (unsigned i = 0; i < repetitions; ++i) {
        const auto start = Clock::now();
        kernel.run(graph, inputs[i % 2], y, cfg, kernel.state.get());
        const auto end = Clock::now();
        result.seconds.push_back(std::chrono::duration<double>(end - start).count());
        // Observe every output after timing. This also gives a consistent warm-start protocol.
        for (std::size_t k = 0; k < y.values.size; ++k) {
            if (!std::isfinite(y.values.data[k]))
                throw std::runtime_error("non-finite timed output");
            result.checksum += y.values.data[k];
        }
    }
    return result;
}
} // namespace member_d

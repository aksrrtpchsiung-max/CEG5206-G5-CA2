#pragma once
#include <ceg5206/contracts.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>
#ifdef CEG5206_D_DIAGNOSTICS
#include "d_profile.hpp"
#endif

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
    unsigned batch_groups = 1, reuse_calls = 1;
    std::vector<double> sequence_seconds;
#ifdef CEG5206_D_DIAGNOSTICS
    std::vector<profile::Snapshot> diagnostics;
#endif
};
inline Samples measure(ceg5206::PreparedKernel& kernel, ceg5206::CsrView graph,
                       const std::vector<ceg5206::DenseConstView>& inputs,
                       ceg5206::DenseView y, const ceg5206::RunConfig& cfg,
                       unsigned warmup, unsigned repetitions,
                       unsigned reuse_calls = 1, double batch_min_seconds = 0) {
    if (!kernel.run || inputs.size() != 2 || warmup < 5 || repetitions < 10 || !reuse_calls || batch_min_seconds < 0)
        throw std::invalid_argument("need a real kernel, two inputs, >=5 warmups and >=10 repetitions");
    for (unsigned i = 0; i < warmup; ++i)
        kernel.run(graph, inputs[i % 2], y, cfg, kernel.state.get());
    Samples result;
    result.seconds.reserve(repetitions);
    result.reuse_calls = reuse_calls;
    // Calibration is outside recorded samples; every group contains exactly R calls.
    if (batch_min_seconds > 0) {
        for (;;) {
            const auto start = Clock::now();
            for (unsigned j = 0; j < result.batch_groups * reuse_calls; ++j)
                kernel.run(graph, inputs[j % 2], y, cfg, kernel.state.get());
            if (std::chrono::duration<double>(Clock::now() - start).count() >= batch_min_seconds ||
                result.batch_groups >= 1024) break;
            result.batch_groups *= 2;
        }
    }
    const unsigned batch_calls = result.batch_groups * reuse_calls;
    for (unsigned i = 0; i < repetitions; ++i) {
        const auto start = Clock::now();
        for (unsigned j = 0; j < batch_calls; ++j)
            kernel.run(graph, inputs[(std::uint64_t(i) * batch_calls + j) % 2], y, cfg, kernel.state.get());
        const auto end = Clock::now();
        const double elapsed = std::chrono::duration<double>(end - start).count();
        result.seconds.push_back(elapsed / batch_calls);
        result.sequence_seconds.push_back(elapsed / result.batch_groups);
#ifdef CEG5206_D_DIAGNOSTICS
        result.diagnostics.push_back(profile::data);
#endif
        // Observe the final output outside each batch. Calls cannot be optimized
        // away: factories/function pointers are compiled without LTO.
        for (std::size_t k = 0; k < y.values.size; ++k) {
            if (!std::isfinite(y.values.data[k]))
                throw std::runtime_error("non-finite timed output");
            result.checksum += y.values.data[k];
        }
    }
    return result;
}
} // namespace member_d

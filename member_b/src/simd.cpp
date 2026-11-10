#include <ceg5206/contracts.hpp>

#include <algorithm>
#include <array>
#include <stdexcept>

#if !defined(__aarch64__) || !defined(__ARM_NEON)
#error "Member B requires AArch64 NEON; select this ISA explicitly for all variants."
#endif
#ifdef __FAST_MATH__
#error "The v0.1 numerical policy prohibits fast-math."
#endif
#include <arm_neon.h>

#if defined(__clang__)
#pragma clang fp contract(off)
#pragma clang fp reassociate(off)
#endif

namespace ceg5206::b {
namespace {

// Constant-size accumulators let the compiler keep the proposal's tuning tiles
// in vector registers. There is no horizontal reduction: each lane is one feature.
template <std::size_t Width>
inline void register_tile(CsrView graph, DenseConstView x, RowOffset begin,
                          RowOffset end, std::size_t feature, float* out) {
    std::array<float32x4_t, Width / 4> accum;
#pragma unroll
    for (std::size_t v = 0; v < Width / 4; ++v) accum[v] = vdupq_n_f32(0.0f);
    for (RowOffset e = begin; e < end; ++e) {
        const auto weight = vdupq_n_f32(graph.values.data[e]);
        const float* input = x.values.data +
            static_cast<std::size_t>(graph.col_idx.data[e]) * x.f + feature;
#pragma unroll
        for (std::size_t v = 0; v < Width / 4; ++v) {
            // Deliberately separate FP32 multiply/add, matching scalar policy.
            accum[v] = vaddq_f32(accum[v], vmulq_f32(weight, vld1q_f32(input + 4 * v)));
        }
    }
#pragma unroll
    for (std::size_t v = 0; v < Width / 4; ++v) vst1q_f32(out + 4 * v, accum[v]);
}

// Arbitrary positive Tf remains supported without unbounded stack storage or
// allocation. Here the live tile is output/cache backed, not promised registers.
// Loads and stores only touch valid features; natural float alignment suffices.
inline void cache_tile(CsrView graph, DenseConstView x, RowOffset begin,
                       RowOffset end, std::size_t feature, std::size_t width,
                       float* out) {
    std::fill_n(out, width, 0.0f);
    const std::size_t vector_end = width - width % 4;
    for (RowOffset e = begin; e < end; ++e) {
        const float w = graph.values.data[e];
        const auto weight = vdupq_n_f32(w);
        const float* input = x.values.data +
            static_cast<std::size_t>(graph.col_idx.data[e]) * x.f + feature;
        for (std::size_t r = 0; r < vector_end; r += 4) {
            vst1q_f32(out + r, vaddq_f32(vld1q_f32(out + r),
                                       vmulq_f32(weight, vld1q_f32(input + r))));
        }
        for (std::size_t r = vector_end; r < width; ++r) out[r] += w * input[r];
    }
}

void run_simd(CsrView graph, DenseConstView x, DenseView y,
              const RunConfig& config, KernelState*) {
    for (std::uint64_t row = 0; row < graph.n; ++row) {
        row_range_simd(graph, x, static_cast<NodeId>(row),
                       graph.row_ptr.data[row], graph.row_ptr.data[row + 1],
                       {y.values.data + static_cast<std::size_t>(row) * x.f, x.f},
                       config.tile_features);
    }
}

} // namespace

void row_range_simd(CsrView graph, DenseConstView x, NodeId /*row*/,
                    RowOffset edge_begin, RowOffset edge_end, Span<float> out,
                    FeatureCount tile_features) {
    // Validated range/shape and positive Tf are caller preconditions. This entry
    // is also C's allocation-free, thread-safe chunk primitive.
    if (edge_begin == edge_end) {
        std::fill_n(out.data, x.f, 0.0f);
        return;
    }
    std::size_t feature = 0;
    while (feature < x.f) {
        const auto width = std::min<std::size_t>(tile_features, x.f - feature);
        float* target = out.data + feature;
        switch (width) {
        case 16: register_tile<16>(graph, x, edge_begin, edge_end, feature, target); break;
        case 32: register_tile<32>(graph, x, edge_begin, edge_end, feature, target); break;
        case 64: register_tile<64>(graph, x, edge_begin, edge_end, feature, target); break;
        default: cache_tile(graph, x, edge_begin, edge_end, feature, width, target); break;
        }
        feature += width;
    }
}

PreparedKernel prepare_simd(CsrView graph, FeatureCount f, const RunConfig& config) {
    if (config.threads != 1 || config.tile_features == 0 ||
        config.max_chunk_edges == 0 || f == 0) {
        throw std::invalid_argument("SIMD requires threads=1, f>0, Tf>0 and Te>0");
    }
    if (graph.n > std::numeric_limits<NodeId>::max()) {
        throw std::invalid_argument("n exceeds contract v0.1 node limit");
    }
    // Check products before any cast or pointer access. Full CSR/input validation
    // remains A/caller's responsibility outside timed calls, per the contract.
    const auto max_elements = static_cast<std::uint64_t>(
        std::numeric_limits<std::ptrdiff_t>::max()) / sizeof(float);
    if (graph.n > max_elements / f ||
        graph.n + 1 > std::numeric_limits<std::size_t>::max() / sizeof(RowOffset)) {
        throw std::overflow_error("SIMD array size is not addressable");
    }
    if (graph.row_ptr.size != graph.n + 1 || !graph.row_ptr.data ||
        graph.col_idx.size != graph.values.size ||
        (graph.values.size && (!graph.col_idx.data || !graph.values.data))) {
        throw std::invalid_argument("Invalid CSR storage shape");
    }
    PreparedKernel prepared;
    prepared.run = run_simd;
    // No descriptors, heap scratch, mutable state or thread runtime required.
    return prepared;
}

} // namespace ceg5206::b

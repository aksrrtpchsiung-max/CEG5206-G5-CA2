#include "kernels.hpp"
#include <algorithm>
#include <arm_neon.h>
// SIMD-only control: repeatedly load/update Y for each edge, without tiling.
void b_probe::edge_major(ceg5206::CsrView graph, ceg5206::DenseConstView x,
                         ceg5206::DenseView y, const ceg5206::RunConfig&,
                         ceg5206::KernelState*) {
    const auto vector_end = x.f - x.f % 4;
    for (std::uint64_t row = 0; row < graph.n; ++row) {
        float* output = y.values.data + row * x.f;
        std::fill_n(output, x.f, 0.0f);
        for (auto e = graph.row_ptr.data[row]; e < graph.row_ptr.data[row + 1]; ++e) {
            const float* input = x.values.data +
                static_cast<std::size_t>(graph.col_idx.data[e]) * x.f;
            const float w = graph.values.data[e];
            const auto weight = vdupq_n_f32(w);
            for (std::size_t r = 0; r < vector_end; r += 4)
                vst1q_f32(output + r, vaddq_f32(vld1q_f32(output + r),
                    vmulq_f32(weight, vld1q_f32(input + r))));
            for (std::size_t r = vector_end; r < x.f; ++r) output[r] += w * input[r];
        }
    }
}

#include "kernels.hpp"
#include <algorithm>
// Compile only this translation unit with auto-vectorization disabled.
void b_probe::scalar(ceg5206::CsrView graph, ceg5206::DenseConstView x,
                     ceg5206::DenseView y, const ceg5206::RunConfig&,
                     ceg5206::KernelState*) {
    for (std::uint64_t row = 0; row < graph.n; ++row) {
        float* output = y.values.data + row * x.f;
        std::fill_n(output, x.f, 0.0f);
        for (auto e = graph.row_ptr.data[row]; e < graph.row_ptr.data[row + 1]; ++e) {
            const float* input = x.values.data +
                static_cast<std::size_t>(graph.col_idx.data[e]) * x.f;
            for (std::size_t r = 0; r < x.f; ++r)
                output[r] += graph.values.data[e] * input[r];
        }
    }
}

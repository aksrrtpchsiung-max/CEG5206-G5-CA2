#include "checks.hpp"
#include <algorithm>

namespace ceg5206::a {
PreparedKernel prepare_scalar(CsrView g, FeatureCount f, const RunConfig& cfg) {
    if (cfg.threads != 1 || !cfg.tile_features || !cfg.max_chunk_edges)
        throw std::invalid_argument("scalar requires one thread and positive tile/chunk sizes");
    detail::require(detail::dimensions<float>(g.n, f));
    detail::require(validate_csr(g));
    return {scalar_csr, nullptr, {}};
}

void scalar_csr(CsrView g, DenseConstView x, DenseView y, const RunConfig&, KernelState*) {
    for (std::size_t row = 0; row < g.n; ++row) {
        float* out = y.values.data + row * x.f;
        std::fill_n(out, x.f, 0.0f);
        for (auto e = g.row_ptr.data[row]; e < g.row_ptr.data[row + 1]; ++e) {
            const float* source = x.values.data + std::size_t(g.col_idx.data[e]) * x.f;
            const float weight = g.values.data[e];
#if defined(_MSC_VER)
#pragma loop(no_vector)
#endif
            for (std::size_t r = 0; r < x.f; ++r)
                out[r] += weight * source[r];
        }
    }
}
} // namespace ceg5206::a

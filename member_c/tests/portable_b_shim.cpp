// Test-only ABI shim for x64 validation. This is not Member B's NEON kernel and
// must never be reported as SIMD performance.
#include <ceg5206/contracts.hpp>

#include <algorithm>
#include <stdexcept>

namespace ceg5206::b {
namespace {

void run_portable(CsrView graph, DenseConstView x, DenseView y,
                  const RunConfig& config, KernelState*) {
    for (std::uint64_t row = 0; row < graph.n; ++row) {
        row_range_simd(graph, x, static_cast<NodeId>(row), graph.row_ptr.data[row],
                       graph.row_ptr.data[row + 1],
                       {y.values.data + static_cast<std::size_t>(row) * x.f, x.f},
                       config.tile_features);
    }
}

}  // namespace

void row_range_simd(CsrView graph, DenseConstView x, NodeId,
                    RowOffset edge_begin, RowOffset edge_end, Span<float> out,
                    FeatureCount) {
    std::fill_n(out.data, x.f, 0.0F);
    for (RowOffset edge = edge_begin; edge < edge_end; ++edge) {
        const float weight = graph.values.data[edge];
        const float* source = x.values.data +
            static_cast<std::size_t>(graph.col_idx.data[edge]) * x.f;
        for (FeatureCount feature = 0; feature < x.f; ++feature) {
            out.data[feature] += weight * source[feature];
        }
    }
}

PreparedKernel prepare_simd(CsrView, FeatureCount f, const RunConfig& config) {
    if (f == 0 || config.threads != 1 || config.tile_features == 0 ||
        config.max_chunk_edges == 0) {
        throw std::invalid_argument("portable B shim received invalid parameters");
    }
    PreparedKernel prepared;
    prepared.run = run_portable;
    return prepared;
}

}  // namespace ceg5206::b

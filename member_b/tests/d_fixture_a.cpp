// Test-only A adapter for a fixed 1024-node directed cycle. Not a graph generator
// or validator for production inputs. Never include this file in D_A_SOURCES.
#ifndef B_D_TEST_ONLY
#error "This adapter is exclusively for B/D integration tests"
#endif
#include "test_data.hpp"
#include "../bench/kernels.hpp"

namespace ceg5206::a {
GeneratedGraph generate_graph(const GraphConfig& cfg) {
    if (cfg.n != 1024 || cfg.m != 1024 || cfg.family != GraphFamily::uniform)
        throw std::invalid_argument("B/D fixture requires uniform n=m=1024");
    std::vector<WeightedEdge> edges;
    for (NodeId i = 0; i < 1024; ++i) edges.push_back({i, (i + 1) % 1024, 1});
    auto p = b_test::from_edges(1024, std::move(edges));
    GeneratedGraph g;
    g.config = cfg;
    g.csr = std::move(p.graph);
    g.oracle_edges = std::move(p.edges);
    return g;
}
std::vector<float> generate_features(std::uint64_t n, FeatureCount f, std::uint64_t seed) {
    if (n != 1024 || (f != 1 && f != 17 && f != 65))
        throw std::invalid_argument("B/D fixture dimensions unsupported");
    return b_test::features(n, f, seed);
}
ValidationResult validate_csr(CsrView g) {
    if (g.n != 1024 || g.row_ptr.size != 1025 || g.col_idx.size != 1024 ||
        g.values.size != 1024 || !g.row_ptr.data || !g.col_idx.data || !g.values.data)
        return {ValidationCode::invalid_shape, kNoIndex, kNoIndex, "fixture shape mismatch"};
    for (std::size_t i = 0; i < 1024; ++i)
        if (g.row_ptr.data[i] != i || g.col_idx.data[i] != (i + 1) % 1024 || g.values.data[i] != 1)
            return {ValidationCode::invalid_shape, i, i, "fixture contents mismatch"};
    if (g.row_ptr.data[1024] != 1024)
        return {ValidationCode::invalid_row_offsets, kNoIndex, 1024, "fixture offset mismatch"};
    return {};
}
ValidationResult validate_problem(CsrView g, DenseConstView x, DenseView y) {
    auto result = validate_csr(g);
    if (!result.ok()) return result;
    if (x.n != g.n || y.n != g.n || !x.f || y.f != x.f ||
        x.values.size != x.n * x.f || y.values.size != x.values.size ||
        !x.values.data || !y.values.data || x.values.data == y.values.data)
        return {ValidationCode::invalid_shape, kNoIndex, kNoIndex, "fixture dense shape mismatch"};
    for (std::size_t i = 0; i < x.values.size; ++i)
        if (!std::isfinite(x.values.data[i]))
            return {ValidationCode::non_finite_input, kNoIndex, i, "fixture X not finite"};
    return {};
}
PreparedKernel prepare_scalar(CsrView, FeatureCount, const RunConfig&) {
    PreparedKernel k;
    k.run = b_probe::scalar;
    return k;
}
void oracle_fp64(EdgeListView edges, DenseConstView x, ReferenceView y) {
    std::fill_n(y.values.data, y.values.size, 0.0);
    for (std::size_t i = 0; i < edges.edges.size; ++i) {
        const auto e = edges.edges.data[i];
        for (std::size_t r = 0; r < x.f; ++r)
            y.values.data[e.row * x.f + r] +=
                static_cast<double>(e.weight) * x.values.data[e.col * x.f + r];
    }
}
ComparisonReport check_close(DenseConstView x, ReferenceConstView ref, Tolerance tolerance) {
    ComparisonReport c;
    c.checked = x.values.size;
    for (std::size_t i = 0; i < x.values.size; ++i) {
        const double error = std::abs(x.values.data[i] - ref.values.data[i]);
        const double scaled = error / (tolerance.atol + tolerance.rtol * std::abs(ref.values.data[i]));
        if (!std::isfinite(x.values.data[i]) || !std::isfinite(ref.values.data[i]) || scaled > 1) ++c.failed;
        c.max_abs_error = std::max(c.max_abs_error, error);
        const double relative = ref.values.data[i] != 0 ? error / std::abs(ref.values.data[i]) :
            (error == 0 ? 0 : std::numeric_limits<double>::infinity());
        c.max_rel_error = std::max(c.max_rel_error, relative);
        if (i == 0 || scaled > c.max_scaled_error) {
            c.max_scaled_error = scaled;
            c.worst_row = i / x.f;
            c.worst_feature = i % x.f;
        }
    }
    c.passed = c.failed == 0;
    return c;
}
DegreeStats degree_stats(CsrView) { return {1024, 1024, 1, 1, 1, 1, 1, 1, 0}; }
} // namespace ceg5206::a

// Test fixture only. This is not A's implementation and must never be used for experiments.
#ifndef D_TEST_ONLY
#error "fake_a.cpp is only for harness tests"
#endif
#include <ceg5206/contracts.hpp>
#include <cmath>
#include <stdexcept>
namespace ceg5206::a {
GeneratedGraph generate_graph(const GraphConfig& cfg) {
    if (cfg.n != 2 || cfg.m != 2) throw std::invalid_argument("test fixture requires n=m=2");
    GeneratedGraph g;
    g.config = cfg;
    g.csr = {2, {0, 1, 2}, {1, 0}, {1, 1}};
    g.oracle_edges = {{0, 1, 1}, {1, 0, 1}};
    return g;
}
std::vector<float> generate_features(std::uint64_t n, FeatureCount f, std::uint64_t seed) {
    if (n != 2 || f != 1) throw std::invalid_argument("test fixture requires n=2,f=1");
    return {float(seed % 7), float(seed % 11)};
}
ValidationResult validate_csr(CsrView) { return {}; }
ValidationResult validate_problem(CsrView, DenseConstView, DenseView) { return {}; }
void fixture_run(CsrView, DenseConstView x, DenseView y, const RunConfig&, KernelState*) {
    // Keep the synthetic test call above coarse clock resolution; not a benchmark.
    volatile unsigned work = 0;
    for (unsigned i = 0; i < 1000; ++i) work += i;
    (void)work;
    y.values.data[0] = x.values.data[1];
    y.values.data[1] = x.values.data[0];
}
PreparedKernel prepare_scalar(CsrView, FeatureCount, const RunConfig&) {
    PreparedKernel k;
    k.run = fixture_run;
    return k;
}
void oracle_fp64(EdgeListView, DenseConstView x, ReferenceView y) {
    y.values.data[0] = x.values.data[1];
    y.values.data[1] = x.values.data[0];
}
ComparisonReport check_close(DenseConstView x, ReferenceConstView ref, Tolerance) {
    ComparisonReport c;
    c.checked = 2;
    for (unsigned i = 0; i < 2; ++i)
        if (!std::isfinite(x.values.data[i]) || x.values.data[i] != ref.values.data[i]) ++c.failed;
    c.passed = c.failed == 0;
    return c;
}
DegreeStats degree_stats(CsrView) { return {2, 2, 1, 1, 1, 1, 1, 1, 0}; }
} // namespace ceg5206::a

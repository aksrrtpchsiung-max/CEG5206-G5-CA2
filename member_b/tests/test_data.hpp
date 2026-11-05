#pragma once
#include <ceg5206/contracts.hpp>
#include <algorithm>
#include <cmath>
#include <random>
#include <stdexcept>

namespace b_test {
using namespace ceg5206;
struct Problem {
    CsrGraph graph;
    // Saved independently BEFORE CSR construction; never decoded from CSR.
    std::vector<WeightedEdge> edges;
};
inline Problem from_edges(std::uint64_t n, std::vector<WeightedEdge> edges) {
    Problem p;
    p.edges = edges;
    std::sort(edges.begin(), edges.end(), [](auto a, auto b) {
        return a.row < b.row || (a.row == b.row && a.col < b.col);
    });
    p.graph.n = n;
    p.graph.row_ptr.assign(n + 1, 0);
    for (auto e : edges) {
        ++p.graph.row_ptr[e.row + 1];
        p.graph.col_idx.push_back(e.col);
        p.graph.values.push_back(e.weight);
    }
    for (std::size_t i = 1; i <= n; ++i) p.graph.row_ptr[i] += p.graph.row_ptr[i - 1];
    return p;
}
inline std::vector<float> features(std::size_t n, FeatureCount f, std::uint64_t seed) {
    std::mt19937_64 engine(seed);
    std::vector<float> x(n * f);
    for (auto& v : x) v = static_cast<float>(engine() >> 40) * 0x1p-23f - 1.0f;
    return x;
}
inline std::vector<double> oracle(const Problem& p, DenseConstView x) {
    std::vector<double> expected(x.values.size, 0.0);
    for (const auto e : p.edges)
        for (std::size_t r = 0; r < x.f; ++r)
            expected[static_cast<std::size_t>(e.row) * x.f + r] +=
                static_cast<double>(e.weight) *
                static_cast<double>(x.values.data[static_cast<std::size_t>(e.col) * x.f + r]);
    return expected;
}
inline double check(const float* actual, const std::vector<double>& expected) {
    double max_scaled = 0;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const double error = std::abs(static_cast<double>(actual[i]) - expected[i]);
        const double scaled = error / (1e-5 + 1e-4 * std::abs(expected[i]));
        if (!std::isfinite(actual[i]) || !std::isfinite(expected[i]) || scaled > 1)
            throw std::runtime_error("FP64 mismatch at element " + std::to_string(i));
        max_scaled = std::max(max_scaled, scaled);
    }
    return max_scaled;
}
} // namespace b_test

#include "checks.hpp"
#include <algorithm>
#include <numeric>
#include <tuple>

namespace ceg5206::a {
CsrGraph detail::csr_from_sorted_edges(EdgeListView input) {
    CsrGraph g;
    g.n = input.n;
    g.row_ptr.assign(input.n + 1, 0);
    g.col_idx.reserve(input.edges.size);
    g.values.reserve(input.edges.size);
    for (std::size_t i = 0; i < input.edges.size; ++i) {
        const auto& e = input.edges.data[i];
        ++g.row_ptr[std::size_t(e.row) + 1];
        g.col_idx.push_back(e.col);
        g.values.push_back(e.weight);
    }
    std::partial_sum(g.row_ptr.begin(), g.row_ptr.end(), g.row_ptr.begin());
    return g;
}

CsrGraph build_csr(EdgeListView input) {
    detail::require(validate_edges(input));
    if (input.edges.size < 2) return detail::csr_from_sorted_edges(input);
    auto before = [](const WeightedEdge& a, const WeightedEdge& b) {
        return std::tie(a.row, a.col) < std::tie(b.row, b.col);
    };
    auto* first = input.edges.data;
    auto* end = first + input.edges.size;
    if (std::is_sorted(first, end, before)) return detail::csr_from_sorted_edges(input);
    std::vector<WeightedEdge> sorted(first, end);
    std::sort(sorted.begin(), sorted.end(), before);
    return detail::csr_from_sorted_edges({input.n, {sorted.data(), sorted.size()}});
}
} // namespace ceg5206::a

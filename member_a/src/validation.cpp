#include "checks.hpp"
#include <algorithm>

namespace ceg5206::a {
using namespace detail;

ValidationResult validate_csr(CsrView g) {
    if (auto r = nodes(g.n); !r.ok()) return r;
    if (g.row_ptr.size != g.n + 1 || g.col_idx.size != g.values.size)
        return error(Code::invalid_shape, "inconsistent CSR lengths");
    for (const auto& r : {storage(g.row_ptr), storage(g.col_idx), storage(g.values)})
        if (!r.ok()) return r;

    const auto* offsets = g.row_ptr.data;
    if (offsets[0] != 0 || offsets[g.n] != g.values.size)
        return error(Code::invalid_row_offsets, "expected first offset 0 and final offset m");
    for (std::size_t i = 0; i < g.n; ++i) {
        if (offsets[i + 1] < offsets[i] || offsets[i + 1] > g.values.size)
            return error(Code::invalid_row_offsets, "invalid row end", i, i + 1);
        for (auto e = offsets[i]; e < offsets[i + 1]; ++e) {
            const auto col = g.col_idx.data[e];
            if (col >= g.n) return error(Code::column_out_of_range, "invalid column", i, e);
            if (e != offsets[i]) {
                const auto prev = g.col_idx.data[e - 1];
                if (col == prev) return error(Code::duplicate_edge, "duplicate column", i, e);
                if (col < prev) return error(Code::unsorted_columns, "columns out of order", i, e);
            }
            if (!std::isfinite(g.values.data[e]))
                return error(Code::non_finite_input, "non-finite weight", i, e);
        }
    }
    return {};
}

ValidationResult validate_edges(EdgeListView list) {
    if (auto r = nodes(list.n); !r.ok()) return r;
    if (auto r = storage(list.edges); !r.ok()) return r;
    bool sorted = true;
    auto key = [](const WeightedEdge& e) { return (std::uint64_t(e.row) << 32) | e.col; };
    for (std::size_t i = 0; i < list.edges.size; ++i) {
        const auto& e = list.edges.data[i];
        if (e.row >= list.n || e.col >= list.n)
            return error(Code::column_out_of_range, "edge endpoint out of range", e.row, i);
        if (!std::isfinite(e.weight)) return error(Code::non_finite_input, "non-finite weight", e.row, i);
        if (i) {
            const auto prev = key(list.edges.data[i - 1]);
            if (key(e) == prev) return error(Code::duplicate_edge, "duplicate edge", e.row, i);
            if (key(e) < prev) sorted = false;
        }
    }
    if (!sorted) {
        std::vector<std::uint64_t> keys(list.edges.size);
        std::transform(list.edges.data, list.edges.data + list.edges.size, keys.begin(), key);
        std::sort(keys.begin(), keys.end());
        const auto repeated = std::adjacent_find(keys.begin(), keys.end());
        if (repeated != keys.end()) return error(Code::duplicate_edge, "duplicate edge", *repeated >> 32);
    }
    return {};
}

ValidationResult validate_problem(CsrView g, DenseConstView x, DenseView y) {
    if (auto r = validate_csr(g); !r.ok()) return r;
    if (x.n != g.n || y.n != g.n || x.f != y.f)
        return error(Code::invalid_shape, "graph, X and Y shapes do not match");
    if (auto r = dense(x); !r.ok()) return r;
    if (auto r = dense(y); !r.ok()) return r;
    if (overlap(y.values, x.values) || overlap(y.values, g.row_ptr) ||
        overlap(y.values, g.values) || overlap(y.values, g.col_idx))
        return error(Code::overlapping_buffers, "Y overlaps input storage");
    return finite_x(x);
}
} // namespace ceg5206::a

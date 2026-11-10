#include "checks.hpp"
#include <algorithm>

namespace ceg5206::a {
void oracle_fp64(EdgeListView list, DenseConstView x, ReferenceView y) {
    detail::require(validate_edges(list));
    if (x.n != list.n || y.n != x.n || y.f != x.f)
        throw std::invalid_argument("edge list, X and reference shapes do not match");
    detail::require(detail::dense(x));
    detail::require(detail::dense(y));
    detail::require(detail::finite_x(x));
    if (detail::overlap(y.values, x.values) || detail::overlap(y.values, list.edges))
        throw std::invalid_argument("reference overlaps input storage");
    if (!y.values.size) return;
    std::fill_n(y.values.data, y.values.size, 0.0);
    for (std::size_t k = 0; k < list.edges.size; ++k) {
        const auto& edge = list.edges.data[k];
        for (std::size_t r = 0; r < x.f; ++r) {
            const double source = x.values.data[std::size_t(edge.col) * x.f + r];
            y.values.data[std::size_t(edge.row) * x.f + r] += double(edge.weight) * source;
        }
    }
}

static double ratio(double numerator, double denominator) {
    if (denominator != 0) return numerator / denominator;
    return numerator == 0 ? 0.0 : std::numeric_limits<double>::infinity();
}

ComparisonReport check_close(DenseConstView actual, ReferenceConstView expected, Tolerance t) {
    detail::require(detail::dense(actual));
    detail::require(detail::dense(expected));
    if (actual.n != expected.n || actual.f != expected.f ||
        !std::isfinite(t.atol) || !std::isfinite(t.rtol) || t.atol < 0 || t.rtol < 0)
        throw std::invalid_argument("incompatible shapes or invalid tolerance");
    ComparisonReport report;
    report.checked = actual.values.size;
    for (std::size_t i = 0; i < report.checked; ++i) {
        const double a = actual.values.data[i], ref = expected.values.data[i];
        double abs_err, rel_err, scaled;
        if (std::isfinite(a) && std::isfinite(ref)) {
            abs_err = std::abs(a - ref);
            rel_err = ratio(abs_err, std::abs(ref));
            const double limit = t.atol + t.rtol * std::abs(ref);
            scaled = ratio(abs_err, limit);
            if (abs_err > limit) ++report.failed;
        } else {
            abs_err = rel_err = scaled = std::numeric_limits<double>::infinity();
            ++report.failed;
        }
        report.max_abs_error = std::max(abs_err, report.max_abs_error);
        report.max_rel_error = std::max(rel_err, report.max_rel_error);
        if (i == 0 || scaled > report.max_scaled_error) {
            report.max_scaled_error = scaled;
            report.worst_row = i / actual.f;
            report.worst_feature = i % actual.f;
        }
    }
    report.passed = report.failed == 0;
    return report;
}
} // namespace ceg5206::a

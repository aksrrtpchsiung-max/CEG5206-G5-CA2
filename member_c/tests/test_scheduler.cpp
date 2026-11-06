#include "ceg5206/c_scheduler.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace agg = ceg5206::aggregation;

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
template <typename Left, typename Right>
void check_equal(const Left& left, const Right& right, const std::string& message) {
    check(left == right, message);
}

void check_vectors_near(
    const std::vector<float>& actual,
    const std::vector<float>& expected,
    float tolerance,
    const std::string& message) {
    if (actual.size() != expected.size()) {
        check(false, message + " (different sizes)");
        return;
    }
    for (std::size_t index = 0; index < actual.size(); ++index) {
        if (std::fabs(actual[index] - expected[index]) > tolerance) {
            check(false, message + " (index " + std::to_string(index) + ")");
            return;
        }
    }
}

using WeightedEdge = std::pair<std::uint32_t, float>;

agg::CSRGraph make_graph(
    std::uint32_t n,
    const std::vector<std::vector<WeightedEdge>>& rows) {
    if (rows.size() != n) {
        throw std::invalid_argument("rows size must equal n");
    }
    agg::CSRGraph graph;
    graph.n = n;
    graph.row_ptr.push_back(0);
    for (const auto& row : rows) {
        for (const auto& [column, weight] : row) {
            graph.col_idx.push_back(column);
            graph.values.push_back(weight);
        }
        graph.row_ptr.push_back(graph.col_idx.size());
    }
    return graph;
}

std::vector<float> make_features(std::uint32_t n, std::size_t feature_dim) {
    std::vector<float> features(static_cast<std::size_t>(n) * feature_dim);
    for (std::uint32_t node = 0; node < n; ++node) {
        for (std::size_t feature = 0; feature < feature_dim; ++feature) {
            const int pattern = static_cast<int>((node * 13U + feature * 7U) % 23U) - 11;
            features[static_cast<std::size_t>(node) * feature_dim + feature] =
                static_cast<float>(pattern) / 8.0F;
        }
    }
    return features;
}

agg::CSRGraph representative_graph() {
    return make_graph(6, {
        {},
        {{1, 1.0F}},
        {{0, 0.5F}, {1, -1.0F}},
        {{0, 0.2F}, {1, 0.2F}, {2, 0.2F}, {3, 0.2F}, {4, 0.2F}},
        {{2, -0.25F}, {5, 0.75F}},
        {{0, 0.125F}, {1, 0.125F}, {2, 0.125F}, {3, 0.125F},
         {4, 0.125F}, {5, 0.125F}},
    });
}

void test_validation() {
    const agg::CSRGraph graph = representative_graph();
    check(agg::validate_graph(graph).ok, "representative graph validates");

    agg::CSRGraph bad = graph;
    bad.row_ptr.pop_back();
    check(!agg::validate_graph(bad).ok, "wrong row_ptr length is rejected");

    bad = graph;
    bad.row_ptr[2] = bad.row_ptr[3] + 1U;
    check(!agg::validate_graph(bad).ok, "non-monotone row_ptr is rejected");

    bad = graph;
    bad.row_ptr.back() -= 1U;
    check(!agg::validate_graph(bad).ok, "wrong final offset is rejected");

    bad = graph;
    bad.col_idx.front() = graph.n;
    check(!agg::validate_graph(bad).ok, "out-of-range column is rejected");

    bad = make_graph(3, {{{0, 1.0F}, {0, 2.0F}}, {}, {}});
    check(!agg::validate_graph(bad).ok, "duplicate column is rejected");

    bad = make_graph(3, {{{2, 1.0F}, {1, 2.0F}}, {}, {}});
    check(!agg::validate_graph(bad).ok, "unsorted columns are rejected");

    bad = graph;
    bad.values.front() = std::numeric_limits<float>::quiet_NaN();
    check(!agg::validate_graph(bad).ok, "non-finite weight is rejected");

    auto features = make_features(graph.n, 17);
    check(agg::validate_features(graph, features, 17).ok, "finite features validate");
    features.front() = std::numeric_limits<float>::infinity();
    check(!agg::validate_features(graph, features, 17).ok, "non-finite feature is rejected");
}

void test_scan_and_chunk_plan() {
    const std::vector<std::uint64_t> offsets =
        agg::exclusive_scan_counts({0, 3, 1, 0, 2});
    check_equal(
        offsets,
        std::vector<std::uint64_t>({0, 0, 3, 4, 4, 6}),
        "exclusive scan offsets are correct");

    const agg::CSRGraph graph = make_graph(4, {
        {},
        {{0, 1.0F}},
        {{0, 0.2F}, {1, 0.2F}, {2, 0.2F}, {3, 0.2F}, {0, 0.2F}},
        {{1, 0.5F}, {2, 0.5F}},
    });
    // This test intentionally permits a duplicate in row 2 because chunk construction
    // depends only on row boundaries. Full graph validation is tested separately.
    const agg::ChunkPlan plan = agg::build_chunk_plan(graph, 2);
    check_equal(plan.chunk_counts, std::vector<std::uint64_t>({0, 1, 3, 1}),
                "chunk counts are correct");
    check_equal(plan.chunk_offsets, std::vector<std::uint64_t>({0, 0, 1, 4, 5}),
                "chunk offsets are correct");
    check_equal(plan.row_partial_offsets, std::vector<std::uint64_t>({0, 0, 0, 3, 3}),
                "partial offsets are correct");
    check_equal(plan.split_row_count, std::uint64_t{1}, "one row is split");
    check_equal(plan.split_chunk_count, std::uint64_t{3}, "three partials are allocated");
    check_equal(plan.chunks.size(), std::size_t{5}, "five total chunks are created");
    check_equal(plan.chunks[1].edge_count(), std::uint64_t{2}, "first split chunk has two edges");
    check_equal(plan.chunks[3].edge_count(), std::uint64_t{1}, "last split chunk has one edge");
    check(!plan.chunks[0].writes_partial(), "unsplit row writes directly");
    check(plan.chunks[1].writes_partial(), "split row writes a partial");

    bool threw = false;
    try {
        static_cast<void>(agg::build_chunk_plan(graph, 0));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "zero chunk limit is rejected");
}

void test_schedulers_for_feature_width(std::size_t feature_dim) {
    const agg::CSRGraph graph = representative_graph();
    const std::vector<float> features = make_features(graph.n, feature_dim);

    std::vector<float> serial;
    std::vector<float> static_rows;
    std::vector<float> dynamic_rows;
    std::vector<float> split_one_thread;
    std::vector<float> split_four_threads;

    const agg::RunStats serial_stats =
        agg::aggregate_rows_serial(graph, features, feature_dim, serial);
    const agg::RunStats static_stats =
        agg::aggregate_rows_static(graph, features, feature_dim, 3, static_rows);
    const agg::RunStats dynamic_stats =
        agg::aggregate_rows_dynamic(graph, features, feature_dim, 4, dynamic_rows);
    const agg::ChunkPlan plan = agg::build_chunk_plan(graph, 2);
    const agg::RunStats split_stats_one = agg::aggregate_split_rows(
        graph, features, feature_dim, plan, 1, split_one_thread);
    const agg::RunStats split_stats_four = agg::aggregate_split_rows(
        graph, features, feature_dim, plan, 4, split_four_threads);

    check_vectors_near(static_rows, serial, 1.0e-6F, "static schedule matches serial");
    check_vectors_near(dynamic_rows, serial, 1.0e-6F, "dynamic schedule matches serial");
    check_vectors_near(split_one_thread, serial, 1.0e-6F, "single-thread split matches serial");
    check_vectors_near(split_four_threads, serial, 1.0e-6F, "parallel split matches serial");
    check_equal(split_one_thread, split_four_threads,
                "split reduction is deterministic across thread counts");

    const auto sum_edges = [](const agg::RunStats& stats) {
        std::uint64_t total = 0;
        for (std::uint64_t count : stats.worker_edges) {
            total += count;
        }
        return total;
    };
    check_equal(sum_edges(serial_stats), graph.edge_count(), "serial edge accounting is complete");
    check_equal(sum_edges(static_stats), graph.edge_count(), "static edge accounting is complete");
    check_equal(sum_edges(dynamic_stats), graph.edge_count(), "dynamic edge accounting is complete");
    check_equal(sum_edges(split_stats_one), graph.edge_count(), "split edge accounting is complete");
    check_equal(sum_edges(split_stats_four), graph.edge_count(), "parallel split accounting is complete");
    check_equal(
        split_stats_four.partial_buffer_bytes,
        plan.split_chunk_count * feature_dim * sizeof(float),
        "partial-buffer byte accounting is correct");

    // Empty row 0 must remain a zero vector.
    for (std::size_t feature = 0; feature < feature_dim; ++feature) {
        check(serial[feature] == 0.0F, "empty row produces zero output");
    }
}

void test_hand_computed_case() {
    const agg::CSRGraph graph = make_graph(3, {
        {},
        {{1, 1.0F}},
        {{0, -0.5F}, {2, 2.0F}},
    });
    const std::vector<float> features = {
        1.0F, 2.0F,
        3.0F, 4.0F,
        5.0F, 6.0F,
    };
    const std::vector<float> expected = {
        0.0F, 0.0F,
        3.0F, 4.0F,
        9.5F, 11.0F,
    };

    std::vector<float> output;
    const agg::ChunkPlan plan = agg::build_chunk_plan(graph, 1);
    agg::aggregate_split_rows(graph, features, 2, plan, 3, output);
    check_vectors_near(output, expected, 1.0e-6F, "signed-weight hand calculation is correct");
}

void test_assignment_and_plan_mismatch() {
    const agg::CSRGraph graph = representative_graph();
    const agg::ChunkPlan plan = agg::build_chunk_plan(graph, 2);
    const agg::ChunkAssignment assignment = agg::assign_chunks_by_edge_count(plan, 3);

    std::uint64_t assigned_edges = 0;
    std::size_t assigned_chunks = 0;
    for (std::size_t worker = 0; worker < assignment.worker_edges.size(); ++worker) {
        assigned_edges += assignment.worker_edges[worker];
        assigned_chunks += assignment.worker_chunk_indices[worker].size();
    }
    check_equal(assigned_edges, graph.edge_count(), "assignment includes every edge");
    check_equal(assigned_chunks, plan.chunks.size(), "assignment includes every chunk");

    agg::CSRGraph changed = graph;
    changed.row_ptr[2] = changed.row_ptr[1];
    const auto features = make_features(changed.n, 1);
    std::vector<float> output;
    bool threw = false;
    try {
        agg::aggregate_split_rows(changed, features, 1, plan, 2, output);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "a chunk plan cannot be reused after row boundaries change");
}

void test_empty_graph() {
    const agg::CSRGraph graph = make_graph(4, {{}, {}, {}, {}});
    const std::vector<float> features = make_features(graph.n, 17);
    const agg::ChunkPlan plan = agg::build_chunk_plan(graph, 8);
    std::vector<float> output;
    const agg::RunStats stats =
        agg::aggregate_split_rows(graph, features, 17, plan, 8, output);
    check_equal(output, std::vector<float>(4U * 17U, 0.0F), "all-empty graph produces zeros");
    check_equal(stats.partial_buffer_bytes, std::uint64_t{0}, "empty graph allocates no partials");
}

}  // namespace

int main() {
    test_validation();
    test_scan_and_chunk_plan();
    test_hand_computed_case();
    test_assignment_and_plan_mismatch();
    test_empty_graph();

    for (std::size_t feature_dim : {std::size_t{1}, std::size_t{17}, std::size_t{65}}) {
        test_schedulers_for_feature_width(feature_dim);
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All C-part scheduler tests passed\n";
    return 0;
}

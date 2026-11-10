#include <ceg5206/contracts.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace ceg5206;

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
struct Problem {
    CsrGraph graph;
    std::vector<WeightedEdge> edges;
};

Problem from_edges(std::uint32_t n, std::vector<WeightedEdge> edges) {
    Problem result;
    result.edges = edges;
    result.graph.n = n;
    result.graph.row_ptr.assign(static_cast<std::size_t>(n) + 1U, 0);
    std::sort(edges.begin(), edges.end(), [](const auto& left, const auto& right) {
        return std::pair{left.row, left.col} < std::pair{right.row, right.col};
    });
    for (const auto& edge : edges) {
        ++result.graph.row_ptr[edge.row + 1U];
    }
    for (std::size_t row = 0; row < n; ++row) {
        result.graph.row_ptr[row + 1] += result.graph.row_ptr[row];
    }
    for (const auto& edge : edges) {
        result.graph.col_idx.push_back(edge.col);
        result.graph.values.push_back(edge.weight);
    }
    return result;
}

std::vector<double> oracle(const Problem& problem, DenseConstView x) {
    std::vector<double> result(static_cast<std::size_t>(x.n) * x.f, 0.0);
    // This path uses the original edge list and never decodes the CSR.
    for (const auto& edge : problem.edges) {
        for (FeatureCount feature = 0; feature < x.f; ++feature) {
            result[static_cast<std::size_t>(edge.row) * x.f + feature] +=
                static_cast<double>(edge.weight) *
                static_cast<double>(x.values.data[
                    static_cast<std::size_t>(edge.col) * x.f + feature]);
        }
    }
    return result;
}

void compare(const std::vector<float>& actual, const std::vector<double>& expected,
             const std::string& label) {
    check(actual.size() == expected.size(), label + ": shape");
    if (actual.size() != expected.size()) {
        return;
    }
    for (std::size_t index = 0; index < actual.size(); ++index) {
        const double error = std::abs(static_cast<double>(actual[index]) - expected[index]);
        const double limit = 1.0e-5 + 1.0e-4 * std::abs(expected[index]);
        if (!std::isfinite(actual[index]) || error > limit) {
            check(false, label + ": mismatch at " + std::to_string(index));
            return;
        }
    }
}

std::vector<float> features(std::uint32_t n, FeatureCount f, std::uint64_t seed) {
    std::vector<float> values(static_cast<std::size_t>(n) * f);
    std::uint64_t state = seed;
    for (float& value : values) {
        state ^= state << 13U;
        state ^= state >> 7U;
        state ^= state << 17U;
        value = static_cast<float>(static_cast<std::int32_t>(state % 2001U) - 1000) /
            1000.0F;
    }
    return values;
}

void audit_plan(CsrView graph, ChunkPlanView plan) {
    check(plan.max_chunk_edges > 0, "audit: positive Te");
    check(plan.row_chunk_ptr.size == graph.n + 1, "audit: row_chunk_ptr shape");
    if (plan.row_chunk_ptr.size != graph.n + 1) {
        return;
    }
    check(plan.row_chunk_ptr.data[0] == 0, "audit: scan starts at zero");
    check(plan.row_chunk_ptr.data[graph.n] == plan.chunks.size,
          "audit: scan ends at descriptor count");
    std::uint64_t expected_slot = 0;
    for (std::uint64_t row = 0; row < graph.n; ++row) {
        const RowOffset row_begin = graph.row_ptr.data[row];
        const RowOffset row_end = graph.row_ptr.data[row + 1];
        const RowOffset degree = row_end - row_begin;
        const RowOffset expected_count = degree / plan.max_chunk_edges +
            static_cast<RowOffset>(degree % plan.max_chunk_edges != 0);
        const RowOffset first = plan.row_chunk_ptr.data[row];
        const RowOffset last = plan.row_chunk_ptr.data[row + 1];
        check(last - first == expected_count, "audit: per-row chunk count");
        RowOffset cursor = row_begin;
        for (RowOffset index = first; index < last; ++index) {
            const auto& chunk = plan.chunks.data[index];
            check(chunk.row == row, "audit: descriptor row");
            check(chunk.edge_begin == cursor, "audit: contiguous edge coverage");
            check(chunk.edge_end > chunk.edge_begin &&
                  chunk.edge_end - chunk.edge_begin <= plan.max_chunk_edges,
                  "audit: nonempty bounded chunk");
            cursor = chunk.edge_end;
            if (expected_count == 1) {
                check(chunk.partial_slot == kNoIndex, "audit: unsplit direct output");
            } else {
                check(chunk.partial_slot == expected_slot,
                      "audit: sequential unique partial slot");
                ++expected_slot;
            }
        }
        check(cursor == row_end, "audit: complete row coverage");
    }
    check(expected_slot == plan.split_chunk_count, "audit: split count");
}

void run_and_check(PrepareFn prepare, const char* label, const Problem& problem,
                   const std::vector<float>& x_values, FeatureCount f,
                   const RunConfig& config) {
    auto prepared = prepare(problem.graph.view(), f, config);
    std::vector<float> output(static_cast<std::size_t>(problem.graph.n) * f);
    const DenseConstView x{problem.graph.n, f, {x_values.data(), x_values.size()}};
    const auto expected = oracle(problem, x);
    for (int repeat = 0; repeat < 25; ++repeat) {
        std::fill(output.begin(), output.end(), std::numeric_limits<float>::quiet_NaN());
        prepared.run(problem.graph.view(), x,
                     {problem.graph.n, f, {output.data(), output.size()}},
                     config, prepared.state.get());
        compare(output, expected, label);
    }
}

void fixture_test() {
    const Problem problem = from_edges(4, {
        {0, 0, 0.5F}, {0, 2, -1.0F}, {2, 0, 1.0F},
        {2, 1, -0.5F}, {2, 3, 2.0F}, {3, 2, 0.25F},
    });
    const std::vector<float> x_values = {
        1.0F, 2.0F, 3.0F, -1.0F, 0.0F, 1.0F,
        2.0F, -2.0F, 4.0F, 0.5F, 1.0F, -1.0F,
    };
    const std::vector<double> hand_expected = {
        -1.5, 3.0, -2.5, 0.0, 0.0, 0.0,
        2.5, 4.0, 0.5, 0.5, -0.5, 1.0,
    };
    const DenseConstView x{4, 3, {x_values.data(), x_values.size()}};
    check(oracle(problem, x) == hand_expected,
          "independent oracle matches A's hand-written fixture");

    const RunConfig config{4, 32, 2};
    run_and_check(c::prepare_static, "fixture static", problem, x_values, 3, config);
    run_and_check(c::prepare_dynamic, "fixture dynamic", problem, x_values, 3, config);
    run_and_check(c::prepare_split, "fixture split", problem, x_values, 3, config);

    auto split = c::prepare_split(problem.graph.view(), 3, config);
    const auto view = c::chunk_plan_view(*split.state);
    audit_plan(problem.graph.view(), view);
    check(view.max_chunk_edges == 2, "fixture Te");
    check(view.row_chunk_ptr.size == 5, "fixture row_chunk_ptr size");
    const std::array<RowOffset, 5> expected_ptr{0, 1, 1, 3, 4};
    check(std::equal(expected_ptr.begin(), expected_ptr.end(), view.row_chunk_ptr.data),
          "fixture exclusive scan");
    check(view.chunks.size == 4, "fixture descriptor count");
    check(view.split_chunk_count == 2, "fixture split chunk count");
    check(view.chunks.data[0].partial_slot == kNoIndex &&
          view.chunks.data[1].partial_slot == 0 &&
          view.chunks.data[2].partial_slot == 1 &&
          view.chunks.data[3].partial_slot == kNoIndex,
          "fixture partial slots");
    check(split.workspace.partials == 2U * 3U * sizeof(float),
          "fixture partial byte accounting");
}

void widths_and_hub_test() {
    std::vector<WeightedEdge> edges;
    for (NodeId col = 0; col < 256; ++col) {
        edges.push_back({2, col, 1.0F / 256.0F});
    }
    for (NodeId row = 3; row < 256; ++row) {
        edges.push_back({row, static_cast<NodeId>((row + 1U) % 256U), -0.25F});
    }
    const Problem problem = from_edges(256, std::move(edges));
    {
        const RunConfig config{4, 32, 16};
        auto split = c::prepare_split(problem.graph.view(), 17, config);
        audit_plan(problem.graph.view(), c::chunk_plan_view(*split.state));
    }
    for (FeatureCount f : {1U, 17U, 65U}) {
        const auto x0 = features(256, f, 5206101);
        const auto x1 = features(256, f, 5206102);
        const RunConfig config{4, 32, 16};
        for (const auto& values : {x0, x1}) {
            run_and_check(c::prepare_static, "width static", problem, values, f, config);
            run_and_check(c::prepare_dynamic, "width dynamic", problem, values, f, config);
            run_and_check(c::prepare_split, "width split", problem, values, f, config);
        }
    }
}

void parameter_test() {
    CsrGraph empty{0, {0}, {}, {}};
    bool threw = false;
    try {
        c::prepare_split(empty.view(), 1, {1, 32, 0});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "zero Te rejected");

    struct Dummy final : KernelState {} dummy;
    threw = false;
    try {
        static_cast<void>(c::chunk_plan_view(dummy));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "non-split state rejected by chunk_plan_view");
}

}  // namespace

int main() {
    fixture_test();
    widths_and_hub_test();
    parameter_test();
    if (failures != 0) {
        std::cerr << failures << " contract integration test(s) failed\n";
        return 1;
    }
    std::cout << "All A-contract/B-ABI/C integration tests passed\n";
    return 0;
}

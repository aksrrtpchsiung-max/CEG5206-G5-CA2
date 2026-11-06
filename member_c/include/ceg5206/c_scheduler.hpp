#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace ceg5206::aggregation {

struct CSRGraph {
    std::uint32_t n = 0;
    std::vector<std::uint64_t> row_ptr;
    std::vector<std::uint32_t> col_idx;
    std::vector<float> values;

    [[nodiscard]] std::uint64_t edge_count() const noexcept;
};

struct ValidationResult {
    bool ok = false;
    std::string message;

    explicit operator bool() const noexcept { return ok; }
};

// Full graph validation belongs outside timed aggregation calls.
// When require_sorted_unique is true, every row must contain strictly increasing
// column IDs, which enforces both sorting and the no-duplicate policy.
[[nodiscard]] ValidationResult validate_graph(
    const CSRGraph& graph,
    bool require_sorted_unique = true);

// Checks the feature-array size and finite-input contract outside timed calls.
[[nodiscard]] ValidationResult validate_features(
    const CSRGraph& graph,
    const std::vector<float>& features,
    std::size_t feature_dim);

// B can replace this function pointer with a SIMD implementation later. The
// kernel must add exactly [edge_begin, edge_end) into output[0:feature_dim].
using ChunkKernel = void (*)(
    const CSRGraph& graph,
    const float* features,
    std::size_t feature_dim,
    std::uint32_t row,
    std::uint64_t edge_begin,
    std::uint64_t edge_end,
    float* output);

void scalar_chunk_kernel(
    const CSRGraph& graph,
    const float* features,
    std::size_t feature_dim,
    std::uint32_t row,
    std::uint64_t edge_begin,
    std::uint64_t edge_end,
    float* output);

[[nodiscard]] std::vector<std::uint64_t> exclusive_scan_counts(
    const std::vector<std::uint64_t>& counts);

struct ChunkDescriptor {
    static constexpr std::uint64_t kDirectOutput =
        std::numeric_limits<std::uint64_t>::max();

    std::uint32_t row = 0;
    std::uint64_t edge_begin = 0;
    std::uint64_t edge_end = 0;
    std::uint64_t partial_index = kDirectOutput;

    [[nodiscard]] std::uint64_t edge_count() const noexcept {
        return edge_end - edge_begin;
    }

    [[nodiscard]] bool writes_partial() const noexcept {
        return partial_index != kDirectOutput;
    }
};

struct ChunkPlan {
    std::uint32_t n = 0;
    std::uint64_t source_edge_count = 0;
    std::uint64_t chunk_edge_limit = 0;

    // A snapshot prevents accidental reuse after the graph row structure changes.
    std::vector<std::uint64_t> source_row_ptr;

    // chunk_counts has n entries; both offset arrays have n + 1 entries.
    std::vector<std::uint64_t> chunk_counts;
    std::vector<std::uint64_t> chunk_offsets;
    std::vector<std::uint64_t> row_partial_offsets;
    std::vector<ChunkDescriptor> chunks;

    std::uint64_t split_row_count = 0;
    std::uint64_t split_chunk_count = 0;

    [[nodiscard]] std::size_t partial_buffer_elements(
        std::size_t feature_dim) const;
};

[[nodiscard]] ChunkPlan build_chunk_plan(
    const CSRGraph& graph,
    std::uint64_t chunk_edge_limit);

struct ChunkAssignment {
    std::vector<std::vector<std::size_t>> worker_chunk_indices;
    std::vector<std::uint64_t> worker_edges;
};

// Deterministic greedy assignment. Larger chunks are placed first on the worker
// with the smallest assigned edge count; ties use the lower worker index.
[[nodiscard]] ChunkAssignment assign_chunks_by_edge_count(
    const ChunkPlan& plan,
    std::size_t worker_count);

struct RunStats {
    std::vector<std::uint64_t> worker_edges;
    std::vector<std::uint64_t> worker_tasks;
    std::uint64_t split_rows = 0;
    std::uint64_t split_chunks = 0;
    std::uint64_t partial_buffer_bytes = 0;
};

// A simple single-thread placeholder for integration and local correctness tests.
RunStats aggregate_rows_serial(
    const CSRGraph& graph,
    const std::vector<float>& features,
    std::size_t feature_dim,
    std::vector<float>& output,
    ChunkKernel kernel = scalar_chunk_kernel);

RunStats aggregate_rows_static(
    const CSRGraph& graph,
    const std::vector<float>& features,
    std::size_t feature_dim,
    std::size_t thread_count,
    std::vector<float>& output,
    ChunkKernel kernel = scalar_chunk_kernel);

RunStats aggregate_rows_dynamic(
    const CSRGraph& graph,
    const std::vector<float>& features,
    std::size_t feature_dim,
    std::size_t thread_count,
    std::vector<float>& output,
    ChunkKernel kernel = scalar_chunk_kernel);

// Split rows write private partial vectors. After all chunk tasks finish, each
// split row has one reduction owner, and partials are merged in chunk order.
RunStats aggregate_split_rows(
    const CSRGraph& graph,
    const std::vector<float>& features,
    std::size_t feature_dim,
    const ChunkPlan& plan,
    std::size_t thread_count,
    std::vector<float>& output,
    ChunkKernel kernel = scalar_chunk_kernel);

}  // namespace ceg5206::aggregation

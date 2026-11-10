#include "ceg5206/c_scheduler.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <utility>

namespace ceg5206::aggregation {
namespace {

std::size_t checked_product(std::size_t left, std::size_t right, const char* name) {
    if (right != 0 && left > std::numeric_limits<std::size_t>::max() / right) {
        throw std::overflow_error(std::string(name) + " size overflows size_t");
    }
    return left * right;
}

std::size_t normalized_worker_count(std::size_t requested, std::size_t task_count) {
    if (task_count == 0) {
        return 1;
    }
    if (requested == 0) {
        requested = static_cast<std::size_t>(std::thread::hardware_concurrency());
    }
    if (requested == 0) {
        requested = 1;
    }
    requested = std::min(requested, task_count);
    return std::max<std::size_t>(requested, 1);
}

void validate_call_sizes(
    const CSRGraph& graph,
    const std::vector<float>& features,
    std::size_t feature_dim,
    ChunkKernel kernel) {
    if (kernel == nullptr) {
        throw std::invalid_argument("chunk kernel must not be null");
    }
    if (feature_dim == 0) {
        throw std::invalid_argument("feature_dim must be positive");
    }
    if (graph.row_ptr.size() != static_cast<std::size_t>(graph.n) + 1U) {
        throw std::invalid_argument("graph row_ptr size does not match n + 1");
    }
    if (graph.row_ptr.empty() || graph.row_ptr.front() != 0 ||
        graph.row_ptr.back() != graph.col_idx.size() ||
        graph.values.size() != graph.col_idx.size()) {
        throw std::invalid_argument("graph CSR arrays have inconsistent sizes or offsets");
    }
    const std::size_t required = checked_product(
        static_cast<std::size_t>(graph.n), feature_dim, "feature array");
    if (features.size() != required) {
        throw std::invalid_argument("feature array size must equal n * feature_dim");
    }
}

template <typename Work>
void run_workers(std::size_t worker_count, Work&& work) {
    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (std::size_t worker = 0; worker < worker_count; ++worker) {
        workers.emplace_back(work, worker);
    }
    for (auto& worker : workers) {
        worker.join();
    }
}

RunStats make_whole_row_stats(std::size_t worker_count) {
    RunStats stats;
    stats.worker_edges.assign(worker_count, 0);
    stats.worker_tasks.assign(worker_count, 0);
    return stats;
}

}  // namespace

std::uint64_t CSRGraph::edge_count() const noexcept {
    return row_ptr.empty() ? 0 : row_ptr.back();
}

ValidationResult validate_graph(const CSRGraph& graph, bool require_sorted_unique) {
    const auto fail = [](std::string message) {
        return ValidationResult{false, std::move(message)};
    };

    if (graph.row_ptr.size() != static_cast<std::size_t>(graph.n) + 1U) {
        return fail("row_ptr size must equal n + 1");
    }
    if (graph.row_ptr.empty() || graph.row_ptr.front() != 0) {
        return fail("row_ptr[0] must be zero");
    }
    if (!std::is_sorted(graph.row_ptr.begin(), graph.row_ptr.end())) {
        return fail("row_ptr must be monotone nondecreasing");
    }
    if (graph.row_ptr.back() != graph.col_idx.size()) {
        return fail("final row offset must equal col_idx size");
    }
    if (graph.values.size() != graph.col_idx.size()) {
        return fail("values size must equal col_idx size");
    }

    for (std::uint32_t row = 0; row < graph.n; ++row) {
        const std::uint64_t begin = graph.row_ptr[row];
        const std::uint64_t end = graph.row_ptr[row + 1U];
        std::uint32_t previous = 0;
        bool has_previous = false;
        for (std::uint64_t edge = begin; edge < end; ++edge) {
            const std::uint32_t column = graph.col_idx[static_cast<std::size_t>(edge)];
            if (column >= graph.n) {
                return fail("column index is outside [0, n)");
            }
            if (!std::isfinite(graph.values[static_cast<std::size_t>(edge)])) {
                return fail("all stored weights must be finite");
            }
            if (require_sorted_unique && has_previous && column <= previous) {
                return fail("column IDs in each row must be strictly increasing");
            }
            previous = column;
            has_previous = true;
        }
    }

    return ValidationResult{true, {}};
}

ValidationResult validate_features(
    const CSRGraph& graph,
    const std::vector<float>& features,
    std::size_t feature_dim) {
    if (feature_dim == 0) {
        return ValidationResult{false, "feature_dim must be positive"};
    }
    std::size_t expected = 0;
    try {
        expected = checked_product(
            static_cast<std::size_t>(graph.n), feature_dim, "feature array");
    } catch (const std::overflow_error& error) {
        return ValidationResult{false, error.what()};
    }
    if (features.size() != expected) {
        return ValidationResult{false, "feature array size must equal n * feature_dim"};
    }
    for (float value : features) {
        if (!std::isfinite(value)) {
            return ValidationResult{false, "all feature values must be finite"};
        }
    }
    return ValidationResult{true, {}};
}

void scalar_chunk_kernel(
    const CSRGraph& graph,
    const float* features,
    std::size_t feature_dim,
    std::uint32_t /*row*/,
    std::uint64_t edge_begin,
    std::uint64_t edge_end,
    float* output) {
    for (std::uint64_t edge = edge_begin; edge < edge_end; ++edge) {
        const auto edge_index = static_cast<std::size_t>(edge);
        const std::size_t source = static_cast<std::size_t>(graph.col_idx[edge_index]);
        const float weight = graph.values[edge_index];
        const float* source_features = features + source * feature_dim;
        for (std::size_t feature = 0; feature < feature_dim; ++feature) {
            output[feature] += weight * source_features[feature];
        }
    }
}

std::vector<std::uint64_t> exclusive_scan_counts(
    const std::vector<std::uint64_t>& counts) {
    std::vector<std::uint64_t> offsets(counts.size() + 1U, 0);
    for (std::size_t index = 0; index < counts.size(); ++index) {
        if (counts[index] > std::numeric_limits<std::uint64_t>::max() - offsets[index]) {
            throw std::overflow_error("exclusive scan overflows uint64_t");
        }
        offsets[index + 1U] = offsets[index] + counts[index];
    }
    return offsets;
}

std::size_t ChunkPlan::partial_buffer_elements(std::size_t feature_dim) const {
    if (split_chunk_count > std::numeric_limits<std::size_t>::max()) {
        throw std::overflow_error("split chunk count exceeds size_t");
    }
    return checked_product(
        static_cast<std::size_t>(split_chunk_count), feature_dim, "partial buffer");
}

ChunkPlan build_chunk_plan(const CSRGraph& graph, std::uint64_t chunk_edge_limit) {
    if (chunk_edge_limit == 0) {
        throw std::invalid_argument("chunk_edge_limit must be positive");
    }
    if (graph.row_ptr.size() != static_cast<std::size_t>(graph.n) + 1U) {
        throw std::invalid_argument("graph row_ptr size does not match n + 1");
    }
    if (graph.row_ptr.empty() || graph.row_ptr.front() != 0 ||
        !std::is_sorted(graph.row_ptr.begin(), graph.row_ptr.end()) ||
        graph.row_ptr.back() != graph.col_idx.size() ||
        graph.values.size() != graph.col_idx.size()) {
        throw std::invalid_argument("graph CSR arrays have inconsistent sizes or offsets");
    }

    ChunkPlan plan;
    plan.n = graph.n;
    plan.source_edge_count = graph.edge_count();
    plan.chunk_edge_limit = chunk_edge_limit;
    plan.source_row_ptr = graph.row_ptr;
    plan.chunk_counts.assign(graph.n, 0);

    std::vector<std::uint64_t> partial_counts(graph.n, 0);
    for (std::uint32_t row = 0; row < graph.n; ++row) {
        const std::uint64_t degree = graph.row_ptr[row + 1U] - graph.row_ptr[row];
        const std::uint64_t count =
            degree == 0 ? 0 : 1U + (degree - 1U) / chunk_edge_limit;
        plan.chunk_counts[row] = count;
        if (count > 1U) {
            partial_counts[row] = count;
            ++plan.split_row_count;
        }
    }

    plan.chunk_offsets = exclusive_scan_counts(plan.chunk_counts);
    plan.row_partial_offsets = exclusive_scan_counts(partial_counts);
    plan.split_chunk_count = plan.row_partial_offsets.back();

    if (plan.chunk_offsets.back() > std::numeric_limits<std::size_t>::max()) {
        throw std::overflow_error("chunk count exceeds size_t");
    }
    plan.chunks.reserve(static_cast<std::size_t>(plan.chunk_offsets.back()));

    for (std::uint32_t row = 0; row < graph.n; ++row) {
        const std::uint64_t row_begin = graph.row_ptr[row];
        const std::uint64_t row_end = graph.row_ptr[row + 1U];
        const std::uint64_t count = plan.chunk_counts[row];
        for (std::uint64_t local = 0; local < count; ++local) {
            const std::uint64_t begin = row_begin + local * chunk_edge_limit;
            const std::uint64_t end = std::min(begin + chunk_edge_limit, row_end);
            const std::uint64_t partial_index = count > 1U
                ? plan.row_partial_offsets[row] + local
                : ChunkDescriptor::kDirectOutput;
            plan.chunks.push_back(ChunkDescriptor{row, begin, end, partial_index});
        }
    }

    return plan;
}

ChunkAssignment assign_chunks_by_edge_count(
    const ChunkPlan& plan,
    std::size_t worker_count) {
    worker_count = normalized_worker_count(worker_count, plan.chunks.size());

    ChunkAssignment assignment;
    assignment.worker_chunk_indices.resize(worker_count);
    assignment.worker_edges.assign(worker_count, 0);

    std::vector<std::size_t> order(plan.chunks.size());
    std::iota(order.begin(), order.end(), 0U);
    std::stable_sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
        return plan.chunks[left].edge_count() > plan.chunks[right].edge_count();
    });

    for (std::size_t chunk_index : order) {
        const auto least_loaded = std::min_element(
            assignment.worker_edges.begin(), assignment.worker_edges.end());
        const std::size_t worker =
            static_cast<std::size_t>(least_loaded - assignment.worker_edges.begin());
        assignment.worker_chunk_indices[worker].push_back(chunk_index);
        assignment.worker_edges[worker] += plan.chunks[chunk_index].edge_count();
    }

    return assignment;
}

RunStats aggregate_rows_serial(
    const CSRGraph& graph,
    const std::vector<float>& features,
    std::size_t feature_dim,
    std::vector<float>& output,
    ChunkKernel kernel) {
    return aggregate_rows_static(
        graph, features, feature_dim, 1, output, kernel);
}

RunStats aggregate_rows_static(
    const CSRGraph& graph,
    const std::vector<float>& features,
    std::size_t feature_dim,
    std::size_t thread_count,
    std::vector<float>& output,
    ChunkKernel kernel) {
    validate_call_sizes(graph, features, feature_dim, kernel);
    const std::size_t output_size = checked_product(
        static_cast<std::size_t>(graph.n), feature_dim, "output");
    output.assign(output_size, 0.0F);

    const std::size_t workers = normalized_worker_count(thread_count, graph.n);
    RunStats stats = make_whole_row_stats(workers);

    run_workers(workers, [&](std::size_t worker) {
        const std::uint32_t begin = static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(graph.n) * worker) / workers);
        const std::uint32_t end = static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(graph.n) * (worker + 1U)) / workers);
        for (std::uint32_t row = begin; row < end; ++row) {
            const std::uint64_t edge_begin = graph.row_ptr[row];
            const std::uint64_t edge_end = graph.row_ptr[row + 1U];
            kernel(
                graph,
                features.data(),
                feature_dim,
                row,
                edge_begin,
                edge_end,
                output.data() + static_cast<std::size_t>(row) * feature_dim);
            stats.worker_edges[worker] += edge_end - edge_begin;
            ++stats.worker_tasks[worker];
        }
    });

    return stats;
}

RunStats aggregate_rows_dynamic(
    const CSRGraph& graph,
    const std::vector<float>& features,
    std::size_t feature_dim,
    std::size_t thread_count,
    std::vector<float>& output,
    ChunkKernel kernel) {
    validate_call_sizes(graph, features, feature_dim, kernel);
    const std::size_t output_size = checked_product(
        static_cast<std::size_t>(graph.n), feature_dim, "output");
    output.assign(output_size, 0.0F);

    const std::size_t workers = normalized_worker_count(thread_count, graph.n);
    RunStats stats = make_whole_row_stats(workers);
    std::atomic<std::uint32_t> next_row{0};

    run_workers(workers, [&](std::size_t worker) {
        while (true) {
            const std::uint32_t row = next_row.fetch_add(1, std::memory_order_relaxed);
            if (row >= graph.n) {
                break;
            }
            const std::uint64_t edge_begin = graph.row_ptr[row];
            const std::uint64_t edge_end = graph.row_ptr[row + 1U];
            kernel(
                graph,
                features.data(),
                feature_dim,
                row,
                edge_begin,
                edge_end,
                output.data() + static_cast<std::size_t>(row) * feature_dim);
            stats.worker_edges[worker] += edge_end - edge_begin;
            ++stats.worker_tasks[worker];
        }
    });

    return stats;
}

RunStats aggregate_split_rows(
    const CSRGraph& graph,
    const std::vector<float>& features,
    std::size_t feature_dim,
    const ChunkPlan& plan,
    std::size_t thread_count,
    std::vector<float>& output,
    ChunkKernel kernel) {
    validate_call_sizes(graph, features, feature_dim, kernel);
    if (plan.n != graph.n || plan.source_edge_count != graph.edge_count() ||
        plan.source_row_ptr != graph.row_ptr) {
        throw std::invalid_argument("chunk plan does not match the graph row structure");
    }

    const std::size_t output_size = checked_product(
        static_cast<std::size_t>(graph.n), feature_dim, "output");
    output.assign(output_size, 0.0F);
    std::vector<float> partials(plan.partial_buffer_elements(feature_dim), 0.0F);

    const std::size_t workers = normalized_worker_count(thread_count, plan.chunks.size());
    const ChunkAssignment assignment = assign_chunks_by_edge_count(plan, workers);

    RunStats stats;
    stats.worker_edges = assignment.worker_edges;
    stats.worker_tasks.resize(workers, 0);
    stats.split_rows = plan.split_row_count;
    stats.split_chunks = plan.split_chunk_count;
    stats.partial_buffer_bytes = static_cast<std::uint64_t>(partials.size()) * sizeof(float);

    run_workers(workers, [&](std::size_t worker) {
        for (std::size_t chunk_index : assignment.worker_chunk_indices[worker]) {
            const ChunkDescriptor& chunk = plan.chunks[chunk_index];
            float* destination = chunk.writes_partial()
                ? partials.data() + static_cast<std::size_t>(chunk.partial_index) * feature_dim
                : output.data() + static_cast<std::size_t>(chunk.row) * feature_dim;
            kernel(
                graph,
                features.data(),
                feature_dim,
                chunk.row,
                chunk.edge_begin,
                chunk.edge_end,
                destination);
            ++stats.worker_tasks[worker];
        }
    });

    // Static row ownership makes the reduction race-free. Each owner merges a
    // split row's partials in increasing chunk order, independent of task order.
    const std::size_t reduction_workers = normalized_worker_count(thread_count, graph.n);
    run_workers(reduction_workers, [&](std::size_t worker) {
        const std::uint32_t begin = static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(graph.n) * worker) / reduction_workers);
        const std::uint32_t end = static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(graph.n) * (worker + 1U)) / reduction_workers);
        for (std::uint32_t row = begin; row < end; ++row) {
            const std::uint64_t count = plan.chunk_counts[row];
            if (count <= 1U) {
                continue;
            }
            float* row_output =
                output.data() + static_cast<std::size_t>(row) * feature_dim;
            const std::uint64_t first = plan.row_partial_offsets[row];
            for (std::uint64_t local = 0; local < count; ++local) {
                const float* partial = partials.data() +
                    static_cast<std::size_t>(first + local) * feature_dim;
                for (std::size_t feature = 0; feature < feature_dim; ++feature) {
                    row_output[feature] += partial[feature];
                }
            }
        }
    });

    return stats;
}

}  // namespace ceg5206::aggregation

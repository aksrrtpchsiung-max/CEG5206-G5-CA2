#include <ceg5206/contracts.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#ifdef CEG5206_D_DIAGNOSTICS
#include "d_profile.hpp"
#endif

#ifdef _OPENMP
#include <omp.h>
#endif

namespace ceg5206::c {
namespace {

std::uint64_t checked_add(std::uint64_t left, std::uint64_t right, const char* label) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        throw std::overflow_error(std::string(label) + " overflow");
    }
    return left + right;
}

std::uint64_t checked_mul(std::uint64_t left, std::uint64_t right, const char* label) {
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
        throw std::overflow_error(std::string(label) + " overflow");
    }
    return left * right;
}

template <typename T>
std::uint64_t capacity_bytes(const std::vector<T>& values) {
    return checked_mul(values.capacity(), sizeof(T), "workspace byte count");
}

void validate_prepare(CsrView graph, FeatureCount f, const RunConfig& config) {
    if (f == 0 || config.threads == 0 || config.tile_features == 0 ||
        config.max_chunk_edges == 0) {
        throw std::invalid_argument("C prepare requires f, threads, Tf and Te to be positive");
    }
    if (graph.n > std::numeric_limits<NodeId>::max()) {
        throw std::invalid_argument("graph.n exceeds the v0.1 NodeId limit");
    }
    if (graph.row_ptr.size != graph.n + 1 || graph.row_ptr.data == nullptr) {
        throw std::invalid_argument("invalid CSR row_ptr storage");
    }
    if (graph.row_ptr.data[0] != 0 ||
        graph.row_ptr.data[graph.n] != graph.col_idx.size ||
        graph.col_idx.size != graph.values.size ||
        (graph.values.size != 0 &&
         (graph.col_idx.data == nullptr || graph.values.data == nullptr))) {
        throw std::invalid_argument("inconsistent CSR storage shape");
    }
    for (std::uint64_t row = 0; row < graph.n; ++row) {
        if (graph.row_ptr.data[row] > graph.row_ptr.data[row + 1]) {
            throw std::invalid_argument("row_ptr must be monotone");
        }
    }
    const auto elements = checked_mul(graph.n, f, "dense element count");
    if (elements > std::numeric_limits<std::size_t>::max()) {
        throw std::overflow_error("dense element count exceeds size_t");
    }
#ifndef _OPENMP
    if (config.threads != 1) {
        throw std::invalid_argument("threads > 1 require an OpenMP build");
    }
#endif
}

void run_static(CsrView graph, DenseConstView x, DenseView y,
                const RunConfig& config, KernelState*) {
#ifdef CEG5206_D_DIAGNOSTICS
    member_d::profile::reset(config.threads); member_d::profile::begin();
#endif
#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(config.threads)
#endif
    for (std::int64_t signed_row = 0;
         signed_row < static_cast<std::int64_t>(graph.n); ++signed_row) {
        const auto row = static_cast<NodeId>(signed_row);
#ifdef CEG5206_D_DIAGNOSTICS
        member_d::profile::edges(graph.row_ptr.data[row + 1] - graph.row_ptr.data[row]);
#endif
        b::row_range_simd(
            graph, x, row, graph.row_ptr.data[row], graph.row_ptr.data[row + 1],
            {y.values.data + static_cast<std::size_t>(row) * x.f, x.f},
            config.tile_features);
    }
#ifdef CEG5206_D_DIAGNOSTICS
    member_d::profile::end(member_d::profile::data.aggregation);
#endif
}

void run_dynamic(CsrView graph, DenseConstView x, DenseView y,
                 const RunConfig& config, KernelState*) {
#ifdef CEG5206_D_DIAGNOSTICS
    member_d::profile::reset(config.threads); member_d::profile::begin();
#endif
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 1) num_threads(config.threads)
#endif
    for (std::int64_t signed_row = 0;
         signed_row < static_cast<std::int64_t>(graph.n); ++signed_row) {
        const auto row = static_cast<NodeId>(signed_row);
#ifdef CEG5206_D_DIAGNOSTICS
        member_d::profile::edges(graph.row_ptr.data[row + 1] - graph.row_ptr.data[row]);
#endif
        b::row_range_simd(
            graph, x, row, graph.row_ptr.data[row], graph.row_ptr.data[row + 1],
            {y.values.data + static_cast<std::size_t>(row) * x.f, x.f},
            config.tile_features);
    }
#ifdef CEG5206_D_DIAGNOSTICS
    member_d::profile::end(member_d::profile::data.aggregation);
#endif
}

struct SplitState final : KernelState {
    FeatureCount f = 0;
    std::uint32_t workers = 1;
    RowOffset max_chunk_edges = 0;
    std::vector<RowOffset> row_chunk_ptr;
    std::vector<ChunkDescriptor> chunks;
    std::uint64_t split_chunk_count = 0;
    std::vector<float> partials;

    // Flat edge-balanced assignment: worker_chunk_ptr partitions indices into
    // chunks. Assignment is built once in prepare and reused by every call.
    std::vector<std::uint64_t> worker_chunk_ptr;
    std::vector<std::uint64_t> worker_chunk_indices;
};

std::unique_ptr<SplitState> build_split_state(
    CsrView graph, FeatureCount f, const RunConfig& config) {
    auto state = std::make_unique<SplitState>();
    state->f = f;
    state->workers = config.threads;
    state->max_chunk_edges = config.max_chunk_edges;
    state->row_chunk_ptr.assign(static_cast<std::size_t>(graph.n) + 1U, 0);

    for (std::uint64_t row = 0; row < graph.n; ++row) {
        const RowOffset degree = graph.row_ptr.data[row + 1] - graph.row_ptr.data[row];
        const RowOffset count = degree / config.max_chunk_edges +
            static_cast<RowOffset>(degree % config.max_chunk_edges != 0);
        state->row_chunk_ptr[row + 1] = checked_add(
            state->row_chunk_ptr[row], count, "chunk count");
    }

    if (state->row_chunk_ptr.back() > std::numeric_limits<std::size_t>::max()) {
        throw std::overflow_error("descriptor count exceeds size_t");
    }
    state->chunks.reserve(static_cast<std::size_t>(state->row_chunk_ptr.back()));

    for (std::uint64_t row64 = 0; row64 < graph.n; ++row64) {
        const auto row = static_cast<NodeId>(row64);
        const RowOffset row_begin = graph.row_ptr.data[row];
        const RowOffset row_end = graph.row_ptr.data[row + 1];
        const RowOffset count =
            state->row_chunk_ptr[row + 1] - state->row_chunk_ptr[row];
        for (RowOffset local = 0; local < count; ++local) {
            const RowOffset begin = checked_add(
                row_begin, checked_mul(local, config.max_chunk_edges, "chunk offset"),
                "chunk begin");
            const RowOffset end = begin + std::min(config.max_chunk_edges, row_end - begin);
            std::uint64_t partial_slot = kNoIndex;
            if (count > 1) {
                partial_slot = state->split_chunk_count++;
            }
            state->chunks.push_back({row, begin, end, partial_slot});
        }
    }

    const std::uint64_t partial_elements = checked_mul(
        state->split_chunk_count, f, "partial element count");
    if (partial_elements > std::numeric_limits<std::size_t>::max()) {
        throw std::overflow_error("partial storage exceeds size_t");
    }
    state->partials.resize(static_cast<std::size_t>(partial_elements));

    // Longest-processing-time greedy assignment using chunk edge counts. Ties
    // are stable and choose the lower worker ID, making the plan reproducible.
    std::vector<std::uint64_t> order(state->chunks.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](std::uint64_t left,
                                                     std::uint64_t right) {
        const auto& a = state->chunks[static_cast<std::size_t>(left)];
        const auto& b_desc = state->chunks[static_cast<std::size_t>(right)];
        return a.edge_end - a.edge_begin > b_desc.edge_end - b_desc.edge_begin;
    });
    std::vector<std::vector<std::uint64_t>> bins(config.threads);
    std::vector<std::uint64_t> loads(config.threads, 0);
    for (std::uint64_t index : order) {
        const auto least = std::min_element(loads.begin(), loads.end());
        const auto worker = static_cast<std::size_t>(least - loads.begin());
        const auto& chunk = state->chunks[static_cast<std::size_t>(index)];
        bins[worker].push_back(index);
        loads[worker] += chunk.edge_end - chunk.edge_begin;
    }
    state->worker_chunk_ptr.assign(static_cast<std::size_t>(config.threads) + 1U, 0);
    for (std::size_t worker = 0; worker < bins.size(); ++worker) {
        state->worker_chunk_ptr[worker + 1] = state->worker_chunk_ptr[worker] +
            static_cast<std::uint64_t>(bins[worker].size());
        state->worker_chunk_indices.insert(state->worker_chunk_indices.end(),
                                           bins[worker].begin(), bins[worker].end());
    }
    return state;
}

void run_split(CsrView graph, DenseConstView x, DenseView y,
               const RunConfig& config, KernelState* base_state) {
    auto* state = static_cast<SplitState*>(base_state);

#ifdef CEG5206_D_DIAGNOSTICS
    member_d::profile::reset(config.threads); member_d::profile::begin();
#endif
    // Only empty and split rows require initialization. Unsplit rows are fully
    // overwritten by B's row-range kernel.
#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(config.threads)
#endif
    for (std::int64_t signed_row = 0;
         signed_row < static_cast<std::int64_t>(graph.n); ++signed_row) {
        const auto row = static_cast<NodeId>(signed_row);
        const auto count = state->row_chunk_ptr[row + 1] - state->row_chunk_ptr[row];
        if (count == 0 || count > 1) {
            std::fill_n(y.values.data + static_cast<std::size_t>(row) * x.f, x.f, 0.0F);
        }
    }

#ifdef CEG5206_D_DIAGNOSTICS
    member_d::profile::end(member_d::profile::data.initialization); member_d::profile::begin();
#endif
    // The loop index is a logical worker bin, so all bins execute even if an
    // OpenMP runtime chooses fewer physical threads than requested.
#ifdef _OPENMP
#pragma omp parallel for schedule(static, 1) num_threads(config.threads)
#endif
    for (std::int64_t signed_worker = 0;
         signed_worker < static_cast<std::int64_t>(state->workers); ++signed_worker) {
        const auto worker = static_cast<std::size_t>(signed_worker);
        for (std::uint64_t position = state->worker_chunk_ptr[worker];
             position < state->worker_chunk_ptr[worker + 1]; ++position) {
            const auto chunk_index = state->worker_chunk_indices[static_cast<std::size_t>(position)];
            const auto& chunk = state->chunks[static_cast<std::size_t>(chunk_index)];
            float* destination = chunk.partial_slot == kNoIndex
                ? y.values.data + static_cast<std::size_t>(chunk.row) * x.f
                : state->partials.data() +
                      static_cast<std::size_t>(chunk.partial_slot) * x.f;
#ifdef CEG5206_D_DIAGNOSTICS
            member_d::profile::edges(chunk.edge_end - chunk.edge_begin);
#endif
            b::row_range_simd(graph, x, chunk.row, chunk.edge_begin, chunk.edge_end,
                              {destination, x.f}, config.tile_features);
        }
    }

#ifdef CEG5206_D_DIAGNOSTICS
    member_d::profile::end(member_d::profile::data.aggregation); member_d::profile::begin();
#endif
    // A single owner handles each split row and merges descriptor-order partials.
#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(config.threads)
#endif
    for (std::int64_t signed_row = 0;
         signed_row < static_cast<std::int64_t>(graph.n); ++signed_row) {
        const auto row = static_cast<NodeId>(signed_row);
        const auto first = state->row_chunk_ptr[row];
        const auto last = state->row_chunk_ptr[row + 1];
        if (last - first <= 1) {
            continue;
        }
        float* row_output = y.values.data + static_cast<std::size_t>(row) * x.f;
        for (RowOffset descriptor = first; descriptor < last; ++descriptor) {
            const auto slot = state->chunks[static_cast<std::size_t>(descriptor)].partial_slot;
            const float* partial = state->partials.data() +
                static_cast<std::size_t>(slot) * x.f;
            for (FeatureCount feature = 0; feature < x.f; ++feature) {
                row_output[feature] += partial[feature];
            }
        }
    }
#ifdef CEG5206_D_DIAGNOSTICS
    member_d::profile::end(member_d::profile::data.merge);
#endif
}

PreparedKernel prepare_rows(CsrView graph, FeatureCount f, const RunConfig& config,
                            AggregationFn run) {
    validate_prepare(graph, f, config);
    PreparedKernel prepared;
    prepared.run = run;
    return prepared;
}

}  // namespace

PreparedKernel prepare_static(CsrView graph, FeatureCount f,
                              const RunConfig& config) {
    return prepare_rows(graph, f, config, run_static);
}

PreparedKernel prepare_dynamic(CsrView graph, FeatureCount f,
                               const RunConfig& config) {
    return prepare_rows(graph, f, config, run_dynamic);
}

PreparedKernel prepare_split(CsrView graph, FeatureCount f,
                             const RunConfig& config) {
    validate_prepare(graph, f, config);
    auto state = build_split_state(graph, f, config);

    WorkspaceBytes workspace;
    workspace.descriptors = checked_add(
        checked_add(capacity_bytes(state->row_chunk_ptr), capacity_bytes(state->chunks),
                    "descriptor workspace"),
        checked_add(capacity_bytes(state->worker_chunk_ptr),
                    capacity_bytes(state->worker_chunk_indices),
                    "assignment workspace"),
        "descriptor workspace");
    workspace.partials = capacity_bytes(state->partials);
    workspace.other = sizeof(SplitState);

    PreparedKernel prepared;
    prepared.run = run_split;
    prepared.workspace = workspace;
    prepared.state = std::move(state);
    return prepared;
}

ChunkPlanView chunk_plan_view(const KernelState& split_state) {
    const auto* state = dynamic_cast<const SplitState*>(&split_state);
    if (state == nullptr) {
        throw std::invalid_argument("chunk_plan_view requires a split-row state");
    }
    return {
        state->max_chunk_edges,
        {state->row_chunk_ptr.data(), state->row_chunk_ptr.size()},
        {state->chunks.data(), state->chunks.size()},
        state->split_chunk_count,
    };
}

}  // namespace ceg5206::c

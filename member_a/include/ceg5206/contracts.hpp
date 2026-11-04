#ifndef CEG5206_CONTRACTS_HPP
#define CEG5206_CONTRACTS_HPP

// Proposed contract v0.1; see docs/INTERFACES.md for normative semantics.
// Declarations only: member A/B/C implementations are separate deliverables.
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace ceg5206 {

inline constexpr char kContractVersion[] = "0.1";
using RowOffset = std::uint64_t;
using NodeId = std::uint32_t;
using FeatureCount = std::uint32_t;
inline constexpr std::uint64_t kNoIndex =
    std::numeric_limits<std::uint64_t>::max();
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
              "The contract requires IEEE-754 FP32.");
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559,
              "The contract requires IEEE-754 FP64.");

// Non-owning contiguous storage. size is the number of elements, not bytes.
template <typename T>
struct Span {
    T* data = nullptr;
    std::size_t size = 0;
};

struct CsrView {
    std::uint64_t n = 0;
    Span<const RowOffset> row_ptr;
    Span<const NodeId> col_idx;
    Span<const float> values;
};

struct CsrGraph {
    std::uint64_t n = 0;
    std::vector<RowOffset> row_ptr;
    std::vector<NodeId> col_idx;
    std::vector<float> values;
    CsrView view() const noexcept {
        return {n, {row_ptr.data(), row_ptr.size()},
                {col_idx.data(), col_idx.size()},
                {values.data(), values.size()}};
    }
};

struct DenseConstView {
    std::uint64_t n = 0;
    FeatureCount f = 0;
    Span<const float> values;
};
struct DenseView {
    std::uint64_t n = 0;
    FeatureCount f = 0;
    Span<float> values;
};
struct ReferenceConstView {
    std::uint64_t n = 0;
    FeatureCount f = 0;
    Span<const double> values;
};
struct ReferenceView {
    std::uint64_t n = 0;
    FeatureCount f = 0;
    Span<double> values;
};

struct WeightedEdge {
    NodeId row = 0;  // Destination/output node.
    NodeId col = 0;  // Source feature node.
    float weight = 0;
};
struct EdgeListView {
    std::uint64_t n = 0;
    Span<const WeightedEdge> edges;
};

enum class ValidationCode {
    ok, invalid_shape, size_overflow, null_storage, invalid_row_offsets,
    column_out_of_range, unsorted_columns, duplicate_edge,
    non_finite_input, overlapping_buffers, invalid_chunk_plan
};
struct ValidationResult {
    ValidationCode code = ValidationCode::ok;
    std::uint64_t row = kNoIndex;
    std::uint64_t index = kNoIndex;
    std::string message;
    bool ok() const noexcept { return code == ValidationCode::ok; }
};

struct Tolerance {
    double atol = 1e-5;
    double rtol = 1e-4;
};
struct ComparisonReport {
    bool passed = false;
    std::uint64_t checked = 0;
    std::uint64_t failed = 0;
    double max_abs_error = 0;
    double max_rel_error = 0;
    double max_scaled_error = 0;
    std::uint64_t worst_row = kNoIndex;
    std::uint64_t worst_feature = kNoIndex;
};

enum class GraphFamily { uniform, skewed };
struct GraphConfig {
    GraphFamily family = GraphFamily::uniform;
    std::uint64_t n = 0;
    std::uint64_t m = 0;
    std::uint64_t graph_seed = 0;
};
struct GraphBuildTimings {
    double sampling_seconds = 0;
    double weight_preparation_seconds = 0;
    double csr_construction_seconds = 0;
};
struct GeneratedGraph {
    GraphConfig config;
    CsrGraph csr;
    // Captured from canonical edges BEFORE CSR construction, never decoded CSR.
    std::vector<WeightedEdge> oracle_edges;
    GraphBuildTimings timings;
};
struct DegreeStats {
    std::uint64_t n = 0;
    std::uint64_t m = 0;
    RowOffset min = 0;
    RowOffset max = 0;
    double mean = 0;
    double median = 0;
    RowOffset p90 = 0;
    RowOffset p99 = 0;
    std::uint64_t empty_rows = 0;
};

struct RunConfig {
    std::uint32_t threads = 1;
    FeatureCount tile_features = 32;  // Example default, not a tuned result.
    RowOffset max_chunk_edges = 128; // Example default, not a tuned result.
};
struct WorkspaceBytes {
    std::uint64_t descriptors = 0;
    std::uint64_t partials = 0;
    std::uint64_t other = 0;
};
struct KernelState {
    virtual ~KernelState() = default;
};
using AggregationFn = void (*)(CsrView, DenseConstView, DenseView,
                              const RunConfig&, KernelState*);
struct PreparedKernel {
    AggregationFn run = nullptr;
    std::unique_ptr<KernelState> state;
    WorkspaceBytes workspace;
};
using PrepareFn = PreparedKernel (*)(CsrView, FeatureCount, const RunConfig&);

// Overwrite exactly f output elements for one contiguous range within one row.
// No allocation, threading, validation or implicit normalization in this call.
using RowRangeFn = void (*)(CsrView, DenseConstView, NodeId, RowOffset, RowOffset,
                           Span<float>, FeatureCount);

struct ChunkDescriptor {
    NodeId row = 0;
    RowOffset edge_begin = 0;
    RowOffset edge_end = 0;
    std::uint64_t partial_slot = kNoIndex;
};
struct ChunkPlanView {
    RowOffset max_chunk_edges = 0;
    Span<const RowOffset> row_chunk_ptr;
    Span<const ChunkDescriptor> chunks;
    std::uint64_t split_chunk_count = 0;
};

namespace a {
ValidationResult validate_csr(CsrView graph);
ValidationResult validate_edges(EdgeListView edges);
ValidationResult validate_problem(CsrView graph, DenseConstView x, DenseView y);
CsrGraph build_csr(EdgeListView edges);
GeneratedGraph generate_graph(const GraphConfig& config);
std::vector<float> generate_features(std::uint64_t n, FeatureCount f,
                                     std::uint64_t seed);
DegreeStats degree_stats(CsrView graph);
void scalar_csr(CsrView graph, DenseConstView x, DenseView y,
                const RunConfig& config, KernelState* state);
PreparedKernel prepare_scalar(CsrView graph, FeatureCount f,
                              const RunConfig& config);
void oracle_fp64(EdgeListView edges, DenseConstView x, ReferenceView y_ref);
ComparisonReport check_close(DenseConstView actual, ReferenceConstView expected,
                             Tolerance tolerance = {});
ValidationResult audit_chunk_plan(CsrView graph, ChunkPlanView plan);
} // namespace a

// Proposed declarations owned and implemented by B and C, not by A.
namespace b {
void row_range_simd(CsrView graph, DenseConstView x, NodeId row,
                    RowOffset edge_begin, RowOffset edge_end, Span<float> out,
                    FeatureCount tile_features);
PreparedKernel prepare_simd(CsrView graph, FeatureCount f,
                            const RunConfig& config);
} // namespace b
namespace c {
PreparedKernel prepare_static(CsrView graph, FeatureCount f,
                              const RunConfig& config);
PreparedKernel prepare_dynamic(CsrView graph, FeatureCount f,
                               const RunConfig& config);
PreparedKernel prepare_split(CsrView graph, FeatureCount f,
                             const RunConfig& config);
ChunkPlanView chunk_plan_view(const KernelState& split_state);
} // namespace c

} // namespace ceg5206
#endif // CEG5206_CONTRACTS_HPP

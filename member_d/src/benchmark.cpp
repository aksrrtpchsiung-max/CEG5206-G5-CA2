#include "measurement.hpp"
#include "allocation_tracker.hpp"
#include <array>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>

using namespace ceg5206;
#ifdef D_ENABLE_EDGE_MAJOR
namespace b_probe {
void edge_major(CsrView, DenseConstView, DenseView, const RunConfig&, KernelState*);
}
#endif
namespace {
#ifdef D_ENABLE_EDGE_MAJOR
PreparedKernel prepare_edge_major(CsrView, FeatureCount, const RunConfig& cfg) {
    if (cfg.threads != 1) throw std::invalid_argument("edge_major requires one thread");
    PreparedKernel k; k.run = b_probe::edge_major; return k;
}
#endif
struct Entry { const char* name; PrepareFn prepare; };
const Entry entries[] = {
    {"scalar",
#ifdef D_ENABLE_A
     a::prepare_scalar
#else
     nullptr
#endif
    },
    {"simd",
#ifdef D_ENABLE_B
     b::prepare_simd
#else
     nullptr
#endif
    },
    {"row_static",
#ifdef D_ENABLE_C
     c::prepare_static
#else
     nullptr
#endif
    },
    {"row_dynamic",
#ifdef D_ENABLE_C
     c::prepare_dynamic
#else
     nullptr
#endif
    },
    {"split_row",
#ifdef D_ENABLE_C
     c::prepare_split
#else
     nullptr
#endif
    }
#ifdef D_ENABLE_EDGE_MAJOR
    ,{"edge_major", prepare_edge_major}
#endif
};
#ifdef D_ENABLE_A
template<class T> void hash_vector(std::uint64_t& h, const std::vector<T>& values) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(values.data());
    for (std::size_t i = 0; i < values.size() * sizeof(T); ++i) {
        h ^= bytes[i];
        h *= UINT64_C(1099511628211);
    }
}
std::uint64_t number(const char* s) {
    const std::string str(s);
    if (str.empty() || str.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("expected unsigned integer");
    return std::stoull(str);
}
std::uint32_t number32(const char* s) {
    const auto x = number(s);
    if (x > UINT32_MAX) throw std::invalid_argument("32-bit parameter overflow");
    return static_cast<std::uint32_t>(x);
}
void require(ValidationResult result) {
    if (!result.ok()) throw std::runtime_error("validation: " + result.message);
}
void real_run(int argc, char** argv, const Entry& entry) {
    if (argc != 15 && argc != 17) throw std::invalid_argument("internal invocation requires 14 arguments; use scripts/run.py");
    const std::string family(argv[2]);
    if (family != "uniform" && family != "skewed") throw std::invalid_argument("invalid graph family");
    const auto n = number(argv[3]), m = number(argv[4]);
    const auto f = number32(argv[5]);
    const auto graph_seed = number(argv[6]);
    const std::array<std::uint64_t, 2> seeds{number(argv[7]), number(argv[8])};
    const RunConfig cfg{number32(argv[9]), number32(argv[10]), number(argv[11])};
    const auto warmup = number32(argv[12]), repetitions = number32(argv[13]);
    const auto budget = number(argv[14]);
    const auto reuse_calls = argc == 17 ? number32(argv[15]) : 1;
    const double batch_min = argc == 17 ? number(argv[16]) / 1000.0 : 0;
    if (!f || !cfg.threads || !cfg.tile_features || !cfg.max_chunk_edges || n > UINT32_MAX ||
        warmup < 5 || repetitions < 10 || !budget)
        throw std::invalid_argument("invalid dimensions, run config, or timing protocol");
    if ((std::string(entry.name) == "scalar" || (std::string(entry.name) == "simd" || std::string(entry.name) == "edge_major")) && cfg.threads != 1)
        throw std::invalid_argument("scalar/simd require one thread");
    const auto elements = member_d::mul(n, f);
    if (elements > SIZE_MAX / sizeof(double)) throw std::overflow_error("host allocation overflow");
    // Lower bound before allocation. Capacity-based checks follow every major allocation.
    const auto graph_logical = member_d::add(member_d::mul(n + 1, 8), member_d::mul(m, 8));
    const auto base_lower = member_d::add(graph_logical, member_d::mul(elements, 12));
    const auto validation_lower = member_d::add(base_lower, member_d::add(member_d::mul(elements, 8), member_d::mul(m, sizeof(WeightedEdge))));
    if (validation_lower > budget) throw std::runtime_error("validation memory lower bound exceeds budget");
    auto generated = a::generate_graph({family == "uniform" ? GraphFamily::uniform : GraphFamily::skewed, n, m, graph_seed});
    auto& graph = generated.csr;
    if (graph.n != n || graph.values.size() != m) throw std::runtime_error("generator shape mismatch");
    require(a::validate_csr(graph.view()));
    const auto graph_bytes = member_d::add(member_d::allocated(graph.row_ptr), member_d::add(member_d::allocated(graph.col_idx), member_d::allocated(graph.values)));
    const auto edge_bytes = member_d::allocated(generated.oracle_edges);
    if (member_d::add(graph_bytes, edge_bytes) > budget) throw std::runtime_error("generated graph exceeds budget");
    auto x0 = a::generate_features(n, f, seeds[0]);
    auto x1 = a::generate_features(n, f, seeds[1]);
    // Digest uses native byte order; compare only on the same platform/build policy.
    std::uint64_t input_digest = UINT64_C(14695981039346656037);
    hash_vector(input_digest, graph.row_ptr);
    hash_vector(input_digest, graph.col_idx);
    hash_vector(input_digest, graph.values);
    hash_vector(input_digest, x0);
    hash_vector(input_digest, x1);
    std::vector<float> y(static_cast<std::size_t>(elements));
    const auto feature_bytes = member_d::add(member_d::allocated(x0), member_d::allocated(x1));
    const auto base_bytes = member_d::add(graph_bytes, member_d::add(feature_bytes, member_d::allocated(y)));
    if (member_d::add(base_bytes, edge_bytes) > budget) throw std::runtime_error("input arrays exceed budget");
    const std::vector<DenseConstView> inputs{{n, f, {x0.data(), x0.size()}}, {n, f, {x1.data(), x1.size()}}};
    const DenseView output{n, f, {y.data(), y.size()}};
    for (auto x : inputs) require(a::validate_problem(graph.view(), x, output));
    PreparedKernel prepared;
    std::vector<double> preparation_samples;
    preparation_samples.reserve(10);
    for (unsigned trial = 0; trial < 10; ++trial) {
        prepared = {};
        const auto start = member_d::Clock::now();
        prepared = entry.prepare(graph.view(), f, cfg);
        preparation_samples.push_back(std::chrono::duration<double>(member_d::Clock::now() - start).count());
    }
    const double prep_seconds = member_d::quantile(preparation_samples, .5);
    if (!prepared.run) throw std::runtime_error("prepare returned null run");
    const auto workspace = member_d::add(prepared.workspace.descriptors, member_d::add(prepared.workspace.partials, prepared.workspace.other));
    const auto retained_validation = member_d::add(base_bytes, member_d::add(workspace, member_d::add(edge_bytes, member_d::mul(elements, sizeof(double)))));
    if (retained_validation > budget) throw std::runtime_error("prepared workspace/validation exceeds budget");
#ifdef D_ENABLE_C
    if (std::string(entry.name) == "split_row") {
        if (!prepared.state) throw std::runtime_error("split state missing");
        require(a::audit_chunk_plan(graph.view(), c::chunk_plan_view(*prepared.state)));
    }
#endif
    std::vector<double> reference(static_cast<std::size_t>(elements));
    const auto validation_bytes = member_d::add(base_bytes, member_d::add(workspace, member_d::add(edge_bytes, member_d::allocated(reference))));
    if (validation_bytes > budget) throw std::runtime_error("actual validation capacity exceeds budget");
    std::array<ComparisonReport, 2> checks;
    for (unsigned pass = 0; pass < 4; ++pass) {
        const unsigned i = pass % 2;
        a::oracle_fp64({n, {generated.oracle_edges.data(), generated.oracle_edges.size()}}, inputs[i], {n, f, {reference.data(), reference.size()}});
        // Repeated, alternating validation catches missing overwrites and state leakage.
        std::fill(y.begin(), y.end(), std::numeric_limits<float>::quiet_NaN());
        prepared.run(graph.view(), inputs[i], output, cfg, prepared.state.get());
        checks[i] = a::check_close({n, f, {y.data(), y.size()}}, {n, f, {reference.data(), reference.size()}});
        if (!checks[i].passed) throw std::runtime_error("correctness failed; no timing result written");
    }
    const auto degree = a::degree_stats(graph.view());
    std::vector<double>().swap(reference);
    std::vector<WeightedEdge>().swap(generated.oracle_edges);
    const auto samples = member_d::measure(prepared, graph.view(), inputs, output, cfg, warmup, repetitions, reuse_calls, batch_min);
    if (member_d::allocation::peak() > budget) throw std::runtime_error("C++ allocation peak exceeds budget");
    const auto median = member_d::quantile(samples.seconds, .5);
    std::cout << std::setprecision(17) << "{\"contract_version\":\"" << kContractVersion
              << "\",\"input_digest_algorithm\":\"fnv1a64-native-v1\",\"input_digest\":\"" << input_digest
              << "\",\"preparation_seconds\":[";
    for (std::size_t i = 0; i < preparation_samples.size(); ++i) { if (i) std::cout << ","; std::cout << preparation_samples[i]; }
    std::cout << "],\"validation\":[";
    for (unsigned i = 0; i < 2; ++i) {
        if (i) std::cout << ',';
        const auto& c = checks[i];
        std::cout << "{\"feature_seed\":" << seeds[i] << ",\"passed\":true,\"checked\":" << c.checked
                  << ",\"failed\":" << c.failed << ",\"atol\":1e-5,\"rtol\":1e-4,\"max_abs_error\":" << c.max_abs_error
                  << ",\"max_scaled_error\":" << c.max_scaled_error << ",\"max_rel_error\":";
        if (std::isfinite(c.max_rel_error)) std::cout << c.max_rel_error; else std::cout << "\"inf\"";
        std::cout << ",\"worst_row\":";
        if (c.worst_row == kNoIndex) std::cout << "null"; else std::cout << c.worst_row;
        std::cout << ",\"worst_feature\":";
        if (c.worst_feature == kNoIndex) std::cout << "null"; else std::cout << c.worst_feature;
        std::cout << '}';
    }
    std::cout << "],\"calls\":[";
    for (std::size_t i = 0; i < samples.seconds.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << "{\"repetition\":" << i << ",\"batch_calls\":" << samples.batch_groups * reuse_calls << ",\"batch_groups\":" << samples.batch_groups << ",\"reuse_calls\":" << reuse_calls << ",\"feature_seed\":" << seeds[(i * samples.batch_groups * reuse_calls) % 2]
                  << ",\"sequence_seconds\":" << samples.sequence_seconds[i] << ",\"call_seconds\":" << samples.seconds[i] << '}';
    }
    std::cout << "],\"summary\":{\"median_seconds\":" << median << ",\"iqr_seconds\":"
              << member_d::quantile(samples.seconds, .75) - member_d::quantile(samples.seconds, .25)
              << ",\"nominal_gflops\":" << (median > 0 ? 2.0 * m * f / (1e9 * median) : 0)
              << ",\"median_sequence_seconds\":" << member_d::quantile(samples.sequence_seconds, .5)
              << ",\"reuse_total_seconds\":" << generated.timings.weight_preparation_seconds + generated.timings.csr_construction_seconds + prep_seconds + member_d::quantile(samples.sequence_seconds, .5)
              << ",\"checksum\":" << samples.checksum << "},\"setup_memory\":{\"graph_sampling_seconds\":" << generated.timings.sampling_seconds
              << ",\"weight_preparation_seconds\":" << generated.timings.weight_preparation_seconds
              << ",\"csr_construction_seconds\":" << generated.timings.csr_construction_seconds
              << ",\"shared_prep_seconds\":" << generated.timings.weight_preparation_seconds + generated.timings.csr_construction_seconds
              << ",\"variant_prep_seconds\":" << prep_seconds << ",\"graph_bytes\":" << graph_bytes
              << ",\"feature_bytes\":" << feature_bytes << ",\"output_bytes\":" << member_d::allocated(y)
              << ",\"descriptor_bytes\":" << prepared.workspace.descriptors << ",\"partial_bytes\":" << prepared.workspace.partials
              << ",\"other_workspace_bytes\":" << prepared.workspace.other << ",\"validation_retained_bytes\":" << validation_bytes
              << ",\"cpp_allocation_peak_bytes\":" << member_d::allocation::peak()
              << ",\"process_peak_rss_bytes\":" << member_d::allocation::rss_peak()
              << ",\"performance_retained_bytes\":" << member_d::add(base_bytes, workspace)
              << "},\"degree\":{\"degree_min\":" << degree.min << ",\"degree_max\":" << degree.max
              << ",\"degree_mean\":" << degree.mean << ",\"degree_median\":" << degree.median
              << ",\"degree_p90\":" << degree.p90 << ",\"degree_p99\":" << degree.p99 << ",\"empty_rows\":" << degree.empty_rows << "}";
#ifdef CEG5206_D_DIAGNOSTICS
    std::cout << ",\"diagnostics\":[";
    for (std::size_t i = 0; i < samples.diagnostics.size(); ++i) {
        if (i) std::cout << ",";
        const auto& d = samples.diagnostics[i];
        std::cout << "{\"initialization_seconds\":" << d.initialization << ",\"aggregation_seconds\":" << d.aggregation << ",\"merge_seconds\":" << d.merge << ",\"worker_edges\":[";
        for (std::size_t j = 0; j < d.worker_edges.size(); ++j) { if (j) std::cout << ","; std::cout << d.worker_edges[j]; }
        std::cout << "]}";
    }
    std::cout << "]";
#endif
    std::cout << "}\n";
}
#endif
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--list") {
            std::cout << '[';
            for (unsigned i = 0; i < sizeof(entries) / sizeof(entries[0]); ++i) {
                if (i) std::cout << ',';
                std::cout << "{\"variant\":\"" << entries[i].name << "\",\"available\":" << (entries[i].prepare ? "true" : "false") << '}';
            }
            std::cout << "]\n";
            return 0;
        }
        if (argc < 2) throw std::invalid_argument("use --list or scripts/run.py");
        const Entry* chosen = nullptr;
        for (const auto& e : entries) if (std::string(argv[1]) == e.name) chosen = &e;
        if (!chosen) throw std::invalid_argument("unknown variant");
        if (!chosen->prepare) { std::cerr << "unavailable: " << chosen->name << " implementation is not linked\n"; return 3; }
#ifdef D_ENABLE_A
        real_run(argc, argv, *chosen);
#endif
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}

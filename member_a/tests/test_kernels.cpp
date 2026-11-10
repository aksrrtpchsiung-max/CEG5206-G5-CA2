#include "test.hpp"
#include <cstdlib>
#include <iomanip>
#include <set>

namespace test {
namespace {
using namespace ceg5206;
static_assert(std::is_same_v<decltype(&a::scalar_csr), AggregationFn>);
static_assert(std::is_same_v<decltype(&a::prepare_scalar), PrepareFn>);
static_assert(std::is_same_v<decltype(&b::prepare_simd), PrepareFn>);
static_assert(std::is_same_v<decltype(&b::row_range_simd), RowRangeFn>);
static_assert(std::is_same_v<decltype(&c::prepare_split), PrepareFn>);
static_assert(std::is_same_v<decltype(&c::prepare_static), PrepareFn>);
static_assert(std::is_same_v<decltype(&c::prepare_dynamic), PrepareFn>);

struct Variant { const char* name; PrepareFn prepare; unsigned threads; };
std::vector<Variant> available() {
    std::vector<Variant> all{{"scalar", a::prepare_scalar, 1}};
#ifdef A_CHECK_B
    all.push_back({"simd", b::prepare_simd, 1});
#endif
#ifdef A_CHECK_C
    std::set<unsigned> counts{1,2,4};
    if (const char* p = std::getenv("A_TEST_PHYSICAL_CORES")) {
        const auto n = std::stoul(p);
        if (!n || n > INT32_MAX) throw std::invalid_argument("invalid A_TEST_PHYSICAL_CORES");
        counts.insert(static_cast<unsigned>(n));
    }
    for (auto threads : counts) {
        all.push_back({"row_static", c::prepare_static, threads});
        all.push_back({"row_dynamic", c::prepare_dynamic, threads});
        all.push_back({"split_row", c::prepare_split, threads});
    }
#endif
    return all;
}

void exercise(const char* label, const CsrGraph& graph, const std::vector<WeightedEdge>& edges,
              FeatureCount f, RowOffset te, const std::vector<float>& x0,
              const std::vector<float>& x1, bool log_errors = false) {
    const auto n = graph.n;
    const auto g = graph.view();
    std::vector<float> y(n*f + 2, -17.0f);
    DenseView output{n, f, {y.data()+1, y.size()-2}};
    std::vector<double> refs[2];
    const std::vector<float>* inputs[] = {&x0, &x1};
    for (int k = 0; k < 2; ++k) {
        DenseConstView x{n, f, read(*inputs[k])};
        check(a::validate_problem(g, x, output).ok(), "invalid test input");
        refs[k].resize(n*f);
        a::oracle_fp64({n, read(edges)}, x, {n, f, write(refs[k])});
    }
    for (const auto& variant : available()) {
        const RunConfig cfg{variant.threads, 16, te};
        auto prepared = variant.prepare(g, f, cfg);
        check(prepared.run != nullptr, "missing run function");
#ifdef A_CHECK_C
        if (variant.prepare == c::prepare_split) {
            check(prepared.state != nullptr, "missing split state");
            auto plan = c::chunk_plan_view(*prepared.state);
            check(a::audit_chunk_plan(g, plan).ok(), "production C chunk audit failed");
            check(plan.max_chunk_edges == te, "unexpected Te");
            check(prepared.workspace.partials >= plan.split_chunk_count*f*sizeof(float), "partial capacity");
        }
#endif
        std::fill(y.begin()+1, y.end()-1, std::numeric_limits<float>::quiet_NaN());
        for (int k : {0,1,0}) {
            prepared.run(g, {n, f, read(*inputs[k])}, output, cfg, prepared.state.get());
            auto report = a::check_close({n, f, {output.values.data, output.values.size}},
                                         {n, f, read(refs[k])});
            check(y.front() == -17 && y.back() == -17, "write outside output span");
            if (log_errors || !report.passed) {
                const auto row = report.worst_row;
                const auto degree = row == kNoIndex ? 0 : graph.row_ptr[row+1] - graph.row_ptr[row];
                std::cout << std::setprecision(12) << label << ',' << variant.name << ',' << cfg.threads
                          << ',' << k << ',' << report.checked << ',' << report.failed
                          << ',' << report.max_abs_error << ',' << report.max_rel_error
                          << ',' << report.max_scaled_error << ',' << row << ',' << report.worst_feature
                          << ',' << degree << '\n';
            }
            check(report.passed, "variant disagrees with independent edge-list oracle");
        }
    }
}
} // namespace

void handoff() {
    check(fixture::version == std::string(kContractVersion), "fixture version");
    const auto& g = fixture::graph;
    const auto f = fixture::f;
    std::vector<double> ref(g.n*f);
    a::oracle_fp64({g.n, read(fixture::edges)}, {g.n, f, read(fixture::x)}, {g.n, f, write(ref)});
    check(ref == fixture::y, "oracle disagrees with hand-calculated answer");
    auto alt = fixture::x;
    for (auto& v : alt) v *= -2;
    exercise("handoff", g, fixture::edges, f, fixture::te, fixture::x, alt);

    // Deliberately corrupt only CSR while retaining the original captured edges.
    auto corrupted = g;
    corrupted.values[0] += 1;
    std::vector<float> y(g.n*f);
    a::scalar_csr(corrupted.view(), {g.n,f,read(fixture::x)}, {g.n,f,write(y)}, {}, nullptr);
    check(!a::check_close({g.n,f,read(y)}, {g.n,f,read(ref)}).passed, "oracle missed CSR corruption");
#ifdef A_CHECK_B
    for (const auto& c : fixture::chunks) {
        if (c.partial_slot == kNoIndex) continue;
        std::vector<float> partial(f, 42);
        b::row_range_simd(g.view(), {g.n,f,read(fixture::x)}, c.row, c.edge_begin, c.edge_end,
                          write(partial), 16);
        check(std::equal(partial.begin(), partial.end(), fixture::partials.begin()+c.partial_slot*f),
              "B partial differs from fixture");
    }
#endif
}

void variants() {
    auto small_case = [](const char* name, std::uint64_t n, std::vector<WeightedEdge> edges, FeatureCount f) {
        auto g = a::build_csr({n, read(edges)});
        exercise(name, g, edges, f, 3, a::generate_features(n,f,71), a::generate_features(n,f,72));
    };
    small_case("empty", 0, {}, 1);
    small_case("isolated", 4, {}, 17);
    small_case("single", 3, {{2,0,-0.25f}}, 65);
    // Degree sequence covers 0, 1, Te-1, Te, Te+1, 2Te and 2Te+1.
    std::vector<WeightedEdge> edges;
    const unsigned degrees[] = {0,1,2,3,4,6,7};
    for (NodeId row = 0; row < 7; ++row)
        for (NodeId col = 0; col < degrees[row]; ++col)
            edges.push_back({row,col,float(int(col % 3)-1) * 0.25f});
    for (FeatureCount f : {1,16,17,64,65,256}) small_case("chunk_boundaries", 7, edges, f);
}

void hub() {
    constexpr FeatureCount f = 17;
    constexpr std::uint64_t n = 8192;
    const auto g = a::generate_graph({GraphFamily::skewed, n, n*32, 5206002});
    const auto stats = a::degree_stats(g.csr.view());
    check(stats.max == 1599 && stats.m == n*32, "hub workload changed");
    auto x0 = a::generate_features(n,f,5206101);
    auto x1 = a::generate_features(n,f,5206102);
    std::cout << "case,variant,threads,input,checked,failed,max_abs,max_rel,max_scaled,"
                 "worst_row,worst_feature,worst_degree\n";
    exercise("skew8192", g.csr, g.oracle_edges, f, 128, x0, x1, true);
}
} // namespace test

#include "test.hpp"
#include <iomanip>

namespace test {
// Optional correctness sweep for A's generator and baseline. The experiment
// driver owns performance runs. This sweep emits one record per feature seed.
void grid() {
    std::cout << "family,n,m,f,graph_seed,feature_seed,degree_max,degree_median,"
                 "degree_p90,degree_p99,retained_array_bytes,checked,failed,max_abs,"
                 "max_rel,max_scaled,worst_row,worst_feature,worst_degree\n";
    for (auto family : {GraphFamily::uniform, GraphFamily::skewed}) {
        const bool uniform = family == GraphFamily::uniform;
        const std::uint64_t graph_seed = uniform ? 5206001 : 5206002;
        for (std::uint64_t n : {8192,65536}) {
            for (unsigned avg : {8,32}) {
                auto generated = a::generate_graph({family,n,n*avg,graph_seed});
                const auto& g = generated.csr;
                const auto stats = a::degree_stats(g.view());
                check(stats.n == n && stats.m == n*avg, "wrong grid graph dimensions");
                for (FeatureCount f : {16,64,256}) {
                    std::vector<float> y(n*f);
                    std::vector<double> ref(n*f);
                    const auto x0 = a::generate_features(n,f,5206101);
                    const auto x1 = a::generate_features(n,f,5206102);
                    const std::uint64_t retained = g.row_ptr.capacity()*sizeof(RowOffset)
                        + g.col_idx.capacity()*sizeof(NodeId) + g.values.capacity()*sizeof(float)
                        + generated.oracle_edges.capacity()*sizeof(WeightedEdge)
                        + (x0.capacity()+x1.capacity()+y.capacity())*sizeof(float)
                        + ref.capacity()*sizeof(double);
                    check(retained <= (UINT64_C(1)<<30), "grid retained arrays exceed 1 GiB");
                    const auto prepared = a::prepare_scalar(g.view(), f, {});
                    for (int k : {0,1}) {
                        DenseConstView x{n,f,read(k == 0 ? x0 : x1)};
                        DenseView output{n,f,write(y)};
                        check(a::validate_problem(g.view(), x, output).ok(), "invalid grid input");
                        a::oracle_fp64({n,read(generated.oracle_edges)}, x, {n,f,write(ref)});
                        std::fill(y.begin(), y.end(), std::numeric_limits<float>::quiet_NaN());
                        prepared.run(g.view(), x, output, {}, prepared.state.get());
                        const auto r = a::check_close({n,f,read(y)}, {n,f,read(ref)});
                        const auto row = r.worst_row;
                        std::cout << std::setprecision(12) << (uniform?"uniform":"skewed") << ',' << n
                                  << ',' << stats.m << ',' << f << ',' << graph_seed << ',' << 5206101+k
                                  << ',' << stats.max << ',' << stats.median << ',' << stats.p90 << ',' << stats.p99
                                  << ',' << retained << ',' << r.checked << ',' << r.failed << ',' << r.max_abs_error
                                  << ',' << r.max_rel_error << ',' << r.max_scaled_error << ',' << row << ','
                                  << r.worst_feature << ',' << g.row_ptr[row+1]-g.row_ptr[row] << '\n';
                        check(r.passed, "grid scalar failed tolerance");
                    }
                }
            }
        }
    }
}
} // namespace test

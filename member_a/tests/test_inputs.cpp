#include "test.hpp"

namespace test {
using Code = ValidationCode;

void validation() {
    auto good = fixture::graph;
    auto rejects = [&](auto edit, Code code) {
        auto g = good;
        edit(g);
        check(a::validate_csr(g.view()).code == code, "CSR error classification");
    };
    check(a::validate_csr(good.view()).ok(), "valid fixture");
    rejects([](auto& g){g.row_ptr[0] = 1;}, Code::invalid_row_offsets);
    rejects([](auto& g){g.row_ptr.back()--;}, Code::invalid_row_offsets);
    rejects([](auto& g){g.row_ptr[2] = 1;}, Code::invalid_row_offsets);
    rejects([](auto& g){g.row_ptr[1] = 50;}, Code::invalid_row_offsets);
    rejects([](auto& g){g.values.pop_back();}, Code::invalid_shape);
    rejects([](auto& g){g.col_idx[0] = 4;}, Code::column_out_of_range);
    rejects([](auto& g){g.col_idx[1] = 0;}, Code::duplicate_edge);
    rejects([](auto& g){std::swap(g.col_idx[0], g.col_idx[1]);}, Code::unsorted_columns);
    rejects([](auto& g){g.values[0] = std::numeric_limits<float>::infinity();}, Code::non_finite_input);
    auto bad_storage = good.view();
    bad_storage.col_idx.data = nullptr;
    check(a::validate_csr(bad_storage).code == Code::null_storage, "null nonempty columns");

    auto edges = fixture::edges;
    std::reverse(edges.begin(), edges.end());
    auto rebuilt = a::build_csr({good.n, read(edges)});
    check(rebuilt.row_ptr == good.row_ptr && rebuilt.col_idx == good.col_idx &&
          rebuilt.values == good.values && edges.front().row == 3, "build CSR from unsorted immutable edges");
    edges.push_back(edges.front());
    check(a::validate_edges({good.n, read(edges)}).code == Code::duplicate_edge, "non-adjacent duplicate");
    expect_throw<std::invalid_argument>([&]{a::build_csr({good.n, read(edges)});}, "duplicate accepted");
    edges.back().row = 99;
    check(!a::validate_edges({good.n, read(edges)}).ok(), "invalid endpoint");
    edges = fixture::edges;
    edges.back().weight = std::numeric_limits<float>::quiet_NaN();
    check(!a::validate_edges({good.n, read(edges)}).ok(), "nonfinite edge weight");

    auto x = fixture::x;
    std::vector<float> y(x.size(), std::numeric_limits<float>::quiet_NaN());
    DenseConstView input{good.n, fixture::f, read(x)};
    DenseView output{good.n, fixture::f, write(y)};
    check(a::validate_problem(good.view(), input, output).ok(), "NaN-prefilled output");
    output.values = write(x);
    check(a::validate_problem(good.view(), input, output).code == Code::overlapping_buffers, "X/Y alias");
    output.values = write(y);
    check(a::validate_problem(good.view(), {good.n,1,{x.data(),4}},
          {good.n,1,{good.values.data(),4}}).code == Code::overlapping_buffers, "Y overlaps CSR");
    output.values.size--;
    check(!a::validate_problem(good.view(), input, output).ok(), "short output buffer");
    output.values = write(y);
    output.f--;
    check(!a::validate_problem(good.view(), input, output).ok(), "wrong output f");
    output.f = fixture::f;
    x[0] = std::numeric_limits<float>::infinity();
    check(a::validate_problem(good.view(), input, output).code == Code::non_finite_input, "nonfinite X");
    x[0] = 0;
    expect_throw<std::overflow_error>([]{a::generate_features(UINT32_MAX, UINT32_MAX, 0);}, "byte overflow");
    expect_throw<std::invalid_argument>([]{a::generate_features(1, 0, 0);}, "zero f");
    expect_throw<std::invalid_argument>([&]{a::prepare_scalar(good.view(), 1, {2, 16, 2});}, "threaded scalar");
    expect_throw<std::invalid_argument>([&]{a::prepare_scalar(good.view(), 1, {1, 0, 2});}, "zero tile");
    expect_throw<std::invalid_argument>([&]{a::prepare_scalar(good.view(), 1, {1, 16, 0});}, "zero chunk");
}

void generator() {
    for (auto family : {GraphFamily::uniform, GraphFamily::skewed}) {
        GraphConfig cfg{family, 8, 9, 42};
        auto g = a::generate_graph(cfg);
        auto repeated = a::generate_graph(cfg);
        const std::vector<NodeId> golden = family == GraphFamily::uniform
            ? std::vector<NodeId>{1,4,6,5,7,7,4,0,4} : std::vector<NodeId>{4,6,5,7,7,0,3,4,5};
        check(g.csr.col_idx == golden, "seeded neighbour sequence changed");
        check(g.csr.col_idx == repeated.csr.col_idx && g.csr.values == repeated.csr.values &&
              g.csr.row_ptr == repeated.csr.row_ptr, "seed repeatability");
        cfg.graph_seed++;
        check(g.csr.col_idx != a::generate_graph(cfg).csr.col_idx, "graph seed ignored");
        check(a::validate_csr(g.csr.view()).ok() && g.csr.col_idx.size() == cfg.m, "generator structure");
        for (std::size_t i = 0; i < cfg.n; ++i) {
            auto first = g.csr.row_ptr[i], last = g.csr.row_ptr[i + 1];
            for (auto k = first; k < last; ++k) {
                const auto& e = g.oracle_edges.at(k);
                check(e.row == i && e.col != i && e.col == g.csr.col_idx[k], "canonical edge orientation");
                check(e.weight == g.csr.values[k] && e.weight == float(1.0 / double(last-first)), "mean weight");
            }
        }
    }
    const auto x = a::generate_features(2, 2, 42);
    const unsigned high_bits[] = {12669407, 10721167, 12618902, 2286276};
    for (int i = 0; i < 4; ++i) check(x[i] == high_bits[i] * 0x1p-23f - 1.0f, "feature golden value");
    check(x != a::generate_features(2, 2, 43), "feature seed ignored");
    auto s = a::degree_stats(fixture::graph.view());
    check(s.n == 4 && s.m == 6 && s.empty_rows == 1 && s.min == 0 && s.max == 3 &&
          s.mean == 1.5 && s.median == 1.5 && s.p90 == 3 && s.p99 == 3, "degree statistics");
    auto empty = a::generate_graph({GraphFamily::uniform, 0, 0, 1});
    check(empty.csr.row_ptr == std::vector<RowOffset>{0} && a::degree_stats(empty.csr.view()).max == 0, "zero nodes");
    expect_throw<std::invalid_argument>([]{a::generate_graph({GraphFamily::skewed, 8, 36, 0});}, "infeasible hub");
    expect_throw<std::invalid_argument>([]{a::generate_graph({GraphFamily::uniform, 2, 3, 0});}, "too many edges");
    expect_throw<std::invalid_argument>([]{a::generate_graph({GraphFamily::skewed, 1, 0, 0});}, "empty second group");
}

void comparison() {
    auto compare = [](std::vector<float> a, std::vector<double> b, Tolerance t) {
        return ceg5206::a::check_close({1, FeatureCount(a.size()), read(a)},
                                       {1, FeatureCount(b.size()), read(b)}, t);
    };
    auto r = compare({0,0}, {0,0}, {0,0});
    check(r.passed && r.worst_row == 0 && r.worst_feature == 0 && r.max_rel_error == 0, "exact zero/tie");
    r = compare({1,2}, {0,0}, {1,0});
    check(!r.passed && r.failed == 1 && r.checked == 2 && r.worst_feature == 1 &&
          std::isinf(r.max_rel_error) && r.max_scaled_error == 2, "inclusive threshold and relative zero");
    r = compare({std::numeric_limits<float>::quiet_NaN(), 1}, {0,std::numeric_limits<double>::infinity()}, {});
    check(r.failed == 2 && std::isinf(r.max_abs_error) && std::isinf(r.max_scaled_error), "nonfinite values");
    r = a::check_close({0,1,{}}, {0,1,{}});
    check(r.passed && r.checked == 0 && r.worst_row == kNoIndex && r.worst_feature == kNoIndex, "empty comparison");
    expect_throw<std::invalid_argument>([&]{compare({0}, {0}, {-1,0});}, "negative tolerance");
    expect_throw<std::invalid_argument>([&]{compare({0}, {0,0}, {});}, "different shapes");
    // The product is not exactly representable in float. Promotion after
    // multiplication would lose the final bit even with a double accumulator.
    const float v = std::nextafter(1.0f, 2.0f);
    const WeightedEdge e{0,0,v};
    double ref = 0;
    a::oracle_fp64({1,{&e,1}}, {1,1,{&v,1}}, {1,1,{&ref,1}});
    check(ref == double(v)*double(v) && ref != double(v*v), "oracle rounded product to FP32");
}

void chunks() {
    auto audit = [](const auto& offsets, const auto& chunks, RowOffset count, RowOffset te) {
        return a::audit_chunk_plan(fixture::graph.view(), {te, read(offsets), read(chunks), count});
    };
    check(audit(fixture::offsets, fixture::chunks, fixture::split_count, fixture::te).ok(), "fixture plan");
    auto reject = [&](auto change) {
        auto copy = fixture::chunks;
        change(copy);
        check(!audit(fixture::offsets, copy, 2, 2).ok(), "invalid descriptor accepted");
    };
    reject([](auto& c){c[1].edge_end--;});
    reject([](auto& c){c[2].edge_begin--;});
    reject([](auto& c){c[2].edge_end++;});
    reject([](auto& c){c[0].partial_slot = 0;});
    reject([](auto& c){c[2].partial_slot = 0;});
    reject([](auto& c){c[1].row = 1;});
    reject([](auto& c){std::swap(c[1], c[2]);});
    reject([](auto& c){c.pop_back();});
    auto offsets = fixture::offsets;
    offsets[2] = kNoIndex;
    check(!audit(offsets, fixture::chunks, 2, 2).ok(), "scan overflow");
    check(!audit(fixture::offsets, fixture::chunks, 3, 2).ok(), "split count");
    check(!audit(fixture::offsets, fixture::chunks, 2, 0).ok(), "zero Te");
}
} // namespace test

#include "contract_fixture.hpp"
#include "test_data.hpp"
#include <iostream>
#include <thread>
#include <sys/mman.h>
#include <unistd.h>

namespace {
using namespace ceg5206;
using b_test::Problem;
std::size_t checks = 0;
double maximum_scaled_error = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
void compare(const float* actual, const std::vector<double>& expected) {
    maximum_scaled_error = std::max(maximum_scaled_error, b_test::check(actual, expected));
    ++checks;
}
template <typename Exception, typename Fn> void rejects(Fn fn) {
    bool caught = false;
    try { fn(); } catch (const Exception&) { caught = true; }
    require(caught, "expected exception was not thrown");
}
constexpr float canary = 12345.0f;

void full_case(const Problem& p, FeatureCount f, FeatureCount tile) {
    RunConfig cfg{1, tile, 128};
    auto prepared = b::prepare_simd(p.graph.view(), f, cfg);
    require(prepared.run && !prepared.state, "invalid prepared kernel");
    require(prepared.workspace.descriptors == 0 && prepared.workspace.partials == 0 &&
            prepared.workspace.other == 0, "unexpected workspace");
    const std::size_t count = p.graph.n * f;
    std::vector<float> storage(count + 2, canary);
    // Both X and Y start one float after an allocation, exercising natural-only
    // alignment and row strides which are not SIMD multiples.
    DenseView y{p.graph.n, f, {storage.data() + 1, count}};
    for (std::uint64_t seed : {5206101, 5206102, 5206101}) {
        auto values = b_test::features(p.graph.n, f, seed);
        values.insert(values.begin(), canary);
        DenseConstView x{p.graph.n, f, {values.data() + 1, count}};
        auto saved_x = values;
        std::fill_n(y.values.data, count, std::numeric_limits<float>::quiet_NaN());
        prepared.run(p.graph.view(), x, y, cfg, prepared.state.get());
        compare(y.values.data, b_test::oracle(p, x));
        require(values == saved_x, "kernel changed input");
        require(storage.front() == canary && storage.back() == canary, "output overrun");
    }
}

void handoff_fixture() {
    Problem p{fixture::graph, fixture::edges};
    DenseConstView x{p.graph.n, fixture::f, {fixture::x.data(), fixture::x.size()}};
    std::vector<double> expected(fixture::expected.begin(), fixture::expected.end());
    auto independent = b_test::oracle(p, x);
    require(independent == expected, "fixture oracle disagrees with hand-written expected Y");
    std::vector<float> y(expected.size(), canary);
    auto k = b::prepare_simd(p.graph.view(), x.f, {});
    k.run(p.graph.view(), x, {x.n, x.f, {y.data(), y.size()}}, {}, nullptr);
    compare(y.data(), expected);
    std::vector<float> merged(y.size(), 0);
    for (auto c : fixture::chunks) {
        std::vector<float> out(x.f, canary);
        b::row_range_simd(p.graph.view(), x, c.row, c.edge_begin, c.edge_end,
                          {out.data(), out.size()}, 32);
        if (c.partial_slot != kNoIndex) {
            std::vector<double> partial(fixture::partials.begin() + c.partial_slot * x.f,
                                        fixture::partials.begin() + (c.partial_slot + 1) * x.f);
            compare(out.data(), partial);
        }
        for (std::size_t r = 0; r < x.f; ++r) merged[c.row * x.f + r] += out[r];
    }
    compare(merged.data(), expected);
}

void ranges_and_threads(const Problem& p, FeatureCount f, FeatureCount tile) {
    auto values = b_test::features(p.graph.n, f, 8901);
    DenseConstView x{p.graph.n, f, {values.data(), values.size()}};
    std::vector<float> merged(values.size(), 0);
    std::vector<float> parallel(values.size(), canary);
    for (NodeId row = 0; row < p.graph.n; ++row) {
        const auto begin = p.graph.row_ptr[row], end = p.graph.row_ptr[row + 1];
        // All possible split points, including empty prefix/suffix.
        for (auto split = begin; split <= end; ++split) {
            std::vector<float> left(f + 2, canary), right(f + 2, canary);
            b::row_range_simd(p.graph.view(), x, row, begin, split, {left.data() + 1, f}, tile);
            b::row_range_simd(p.graph.view(), x, row, split, end, {right.data() + 1, f}, tile);
            for (std::size_t r = 0; r < f; ++r) merged[row * f + r] = left[r + 1] + right[r + 1];
            require(left.front() == canary && left.back() == canary &&
                    right.front() == canary && right.back() == canary, "partial overrun");
            const auto reference = b_test::oracle(p, x);
            compare(merged.data() + row * f,
                    {reference.begin() + row * f, reference.begin() + (row + 1) * f});
        }
    }
    std::vector<std::thread> workers;
    for (NodeId worker = 0; worker < 4; ++worker) workers.emplace_back([&, worker] {
        for (int repeat = 0; repeat < 10; ++repeat)
            for (NodeId row = worker; row < p.graph.n; row += 4)
                b::row_range_simd(p.graph.view(), x, row, p.graph.row_ptr[row],
                                  p.graph.row_ptr[row + 1], {parallel.data() + row * f, f}, tile);
    });
    for (auto& worker : workers) worker.join();
    compare(parallel.data(), b_test::oracle(p, x));
    // Same row, different chunks and private output slots, as required by C.
    const NodeId row = 2;
    const auto begin = p.graph.row_ptr[row], end = p.graph.row_ptr[row + 1];
    std::vector<float> partials(4 * f, canary);
    workers.clear();
    for (std::size_t w = 0; w < 4; ++w) workers.emplace_back([&, w] {
        b::row_range_simd(p.graph.view(), x, row, begin + (end - begin) * w / 4,
                          begin + (end - begin) * (w + 1) / 4, {partials.data() + w * f, f}, tile);
    });
    for (auto& worker : workers) worker.join();
    std::vector<float> sum(f, 0);
    for (std::size_t w = 0; w < 4; ++w)
        for (std::size_t r = 0; r < f; ++r) sum[r] += partials[w * f + r];
    const auto reference = b_test::oracle(p, x);
    compare(sum.data(), {reference.begin() + row * f, reference.begin() + (row + 1) * f});
}

// Last logical element touches a protected page. Unlike a canary, this catches
// over-reads as well as over-writes even in an optimized non-sanitizer build.
class GuardedFloats {
    void* mapping_;
    std::size_t bytes_;
public:
    float* data;
    explicit GuardedFloats(std::size_t count) {
        const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
        const auto payload = ((count * sizeof(float) + page - 1) / page) * page;
        bytes_ = payload + page;
        mapping_ = mmap(nullptr, bytes_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (mapping_ == MAP_FAILED) throw std::runtime_error("mmap failed");
        auto* guard = static_cast<char*>(mapping_) + payload;
        if (mprotect(guard, page, PROT_NONE) != 0) {
            munmap(mapping_, bytes_);
            throw std::runtime_error("mprotect failed");
        }
        data = reinterpret_cast<float*>(guard) - count;
    }
    GuardedFloats(const GuardedFloats&) = delete;
    ~GuardedFloats() { munmap(mapping_, bytes_); }
};

void guarded_tails() {
    const auto p = b_test::from_edges(1, {{0, 0, -0.5f}});
    for (FeatureCount f = 1; f <= 131; ++f) {
        GuardedFloats input(f), output(f);
        std::fill_n(input.data, f, 0.25f);
        for (FeatureCount tile : {1u, 3u, 4u, 16u, 17u, 32u, 64u, 65u, 256u}) {
            std::fill_n(output.data, f, canary);
            b::row_range_simd(p.graph.view(), {1, f, {input.data, f}}, 0, 0, 1,
                              {output.data, f}, tile);
            compare(output.data, std::vector<double>(f, -0.125));
        }
    }
}

void parameters() {
    CsrGraph empty{0, {0}, {}, {}};
    auto k = b::prepare_simd(empty.view(), 1, {});
    k.run(empty.view(), {0, 1, {nullptr, 0}}, {0, 1, {nullptr, 0}}, {}, nullptr);
    ++checks;
    for (auto cfg : {RunConfig{0, 32, 128}, RunConfig{2, 32, 128},
                     RunConfig{1, 0, 128}, RunConfig{1, 32, 0}})
        rejects<std::invalid_argument>([&] { b::prepare_simd(empty.view(), 1, cfg); });
    rejects<std::invalid_argument>([&] { b::prepare_simd(empty.view(), 0, {}); });
    auto bad = empty.view();
    bad.row_ptr = {nullptr, 1};
    rejects<std::invalid_argument>([&] { b::prepare_simd(bad, 1, {}); });
    bad = empty.view(); bad.row_ptr.size = 0;
    rejects<std::invalid_argument>([&] { b::prepare_simd(bad, 1, {}); });
    bad = empty.view(); bad.n = std::uint64_t{1} << 32;
    rejects<std::invalid_argument>([&] { b::prepare_simd(bad, 1, {}); });
    bad.n = std::numeric_limits<NodeId>::max();
    rejects<std::overflow_error>([&] {
        b::prepare_simd(bad, std::numeric_limits<FeatureCount>::max(), {});
    });
    bad = empty.view(); bad.values.size = 1;
    rejects<std::invalid_argument>([&] { b::prepare_simd(bad, 1, {}); });
    bad.col_idx.size = 1;
    rejects<std::invalid_argument>([&] { b::prepare_simd(bad, 1, {}); });
}
} // namespace

int main() {
    try {
        parameters();
        handoff_fixture();
        auto signed_case = b_test::from_edges(8, {{2, 7, -.25f}, {0, 0, .5f},
            {2, 0, 1.f}, {0, 2, -1.f}, {2, 1, -.5f}, {7, 7, .125f},
            {4, 6, -.75f}, {2, 3, 2.f}, {3, 2, .25f}, {4, 1, .25f}});
        auto saved = signed_case.graph;
        std::vector<FeatureCount> widths;
        for (FeatureCount f = 1; f <= 70; ++f) widths.push_back(f);
        for (FeatureCount f : {127u, 128u, 129u, 255u, 256u, 257u}) widths.push_back(f);
        for (auto f : widths)
            for (FeatureCount tile : {1u, 2u, 3u, 4u, 7u, 15u, 16u, 17u, 31u, 32u,
                                      33u, 63u, 64u, 65u, 257u, UINT32_MAX})
                full_case(signed_case, f, tile);
        for (auto f : {1u, 17u, 65u, 256u})
            for (auto tile : {3u, 16u, 32u, 64u, 65u}) {
                full_case(b_test::from_edges(0, {}), f, tile);
                full_case(b_test::from_edges(5, {}), f, tile);
                full_case(b_test::from_edges(3, {{0, 2, -.5f}}), f, tile);
                ranges_and_threads(signed_case, f, tile);
            }
        // High-degree normalized hub; explicit self-loop, many isolated rows.
        std::vector<WeightedEdge> edges;
        for (NodeId col = 0; col < 4096; ++col) edges.push_back({2, col, 1.f / 4096});
        const auto hub = b_test::from_edges(4096, std::move(edges));
        for (auto f : {1u, 17u, 65u, 256u})
            for (auto tile : {16u, 32u, 64u}) full_case(hub, f, tile);
        // Multiple independent topologies and signed, bounded stored weights.
        std::mt19937_64 random(5206009);
        for (unsigned graph_id = 0; graph_id < 12; ++graph_id) {
            std::vector<WeightedEdge> random_edges;
            for (NodeId row = 0; row < 37; ++row)
                for (NodeId col = 0; col < 37; ++col)
                    if (random() % 7 == 0) random_edges.push_back(
                        {row, col, static_cast<float>(static_cast<int>(random() % 33) - 16) / 32});
            std::reverse(random_edges.begin(), random_edges.end());
            const auto p = b_test::from_edges(37, std::move(random_edges));
            for (auto f : {1u, 17u, 65u, 256u})
                for (auto tile : {16u, 32u, 64u}) full_case(p, f, tile);
        }
        require(signed_case.graph.row_ptr == saved.row_ptr &&
                signed_case.graph.col_idx == saved.col_idx &&
                signed_case.graph.values == saved.values, "kernel changed CSR");
        guarded_tails();
        std::cout << "PASS " << checks << " checks; max_scaled_error="
                  << maximum_scaled_error << "; tolerance=1e-5+1e-4*abs(reference)\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}

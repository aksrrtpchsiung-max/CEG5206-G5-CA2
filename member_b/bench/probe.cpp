#include "kernels.hpp"
#include "../tests/test_data.hpp"
#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <set>

namespace {
using namespace ceg5206;
using Clock = std::chrono::steady_clock;
volatile double observed = 0;

// Diagnostic-only generator: fixed first ceil(n/100) hubs. This intentionally
// has its own version/name; it is NOT A's random-hub a-v0.1 generator.
b_test::Problem make_graph(std::uint64_t n, std::uint64_t degree, bool skewed) {
    const auto m = n * degree, hubs = (n + 99) / 100;
    std::mt19937_64 random(5206801 + static_cast<unsigned>(skewed));
    auto draw = [&](std::uint64_t bound) {
        const auto threshold = (-bound) % bound;
        std::uint64_t u;
        do { u = random(); } while (u < threshold);
        return u % bound;
    };
    std::vector<WeightedEdge> edges;
    edges.reserve(m);
    for (NodeId row = 0; row < n; ++row) {
        std::uint64_t count = degree;
        if (skewed) {
            const bool hub = row < hubs;
            const auto group_n = hub ? hubs : n - hubs;
            const auto group_m = hub ? m / 2 : m - m / 2;
            const auto position = hub ? row : row - hubs;
            count = group_m / group_n + (position < group_m % group_n);
        }
        if (count > n - 1) throw std::invalid_argument("diagnostic graph capacity exceeded");
        std::set<std::uint64_t> selected;
        for (auto j = n - 1 - count; j < n - 1; ++j) {
            const auto t = draw(j + 1);
            if (!selected.insert(t).second) selected.insert(j);
        }
        const float weight = count ? static_cast<float>(1.0 / static_cast<double>(count)) : 0;
        for (auto col : selected)
            edges.push_back({row, static_cast<NodeId>(col < row ? col : col + 1), weight});
    }
    return b_test::from_edges(n, std::move(edges));
}
double quantile(std::vector<double> samples, double q) {
    std::sort(samples.begin(), samples.end());
    const double at = (samples.size() - 1) * q;
    const auto lo = static_cast<std::size_t>(at), hi = std::min(lo + 1, samples.size() - 1);
    return samples[lo] + (samples[hi] - samples[lo]) * (at - lo);
}
void consume(const std::vector<float>& y) {
    double sum = 0;
    for (float value : y) sum += value;
    observed = sum;
}
std::uint64_t number(const char* argument) {
    const std::string s(argument);
    if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("expected unsigned integer");
    return std::stoull(s);
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 1 && argc != 3) throw std::invalid_argument("usage: b_tiling_probe [nodes mean_degree]");
        const std::uint64_t n = argc == 3 ? number(argv[1]) : 8192;
        const std::uint64_t degree = argc == 3 ? number(argv[2]) : 8;
        // Bound validation/test helper memory well below the project budget.
        if (n < 128 || n > 65536 || degree == 0 || degree > 32 || degree >= n)
            throw std::invalid_argument("probe supports 128<=n<=65536, 1<=degree<=32");
        std::cout << "generator,family,n,m,f,variant,tile_features,repetition,feature_seed,call_seconds,median_seconds,iqr_seconds,max_scaled_error\n";
        std::cout << std::setprecision(17);
        for (bool skewed : {false, true}) {
            const auto p = make_graph(n, degree, skewed);
            for (FeatureCount f : {16u, 64u, 256u}) {
                std::array<std::vector<float>, 2> xs{
                    b_test::features(n, f, 5206811), b_test::features(n, f, 5206812)};
                std::vector<float> y(n * f);
                DenseView output{n, f, {y.data(), y.size()}};
                struct Variant { const char* name; AggregationFn run; FeatureCount tile; };
                const std::array<Variant, 5> variants{{
                    {"scalar_control", b_probe::scalar, 32}, {"edge_major_neon", b_probe::edge_major, 32},
                    {"tiled_neon", nullptr, 16}, {"tiled_neon", nullptr, 32}, {"tiled_neon", nullptr, 64}}};
                for (auto variant : variants) {
                    const RunConfig cfg{1, variant.tile, 128};
                    auto prepared = b::prepare_simd(p.graph.view(), f, cfg);
                    if (variant.run) prepared.run = variant.run;
                    double error = 0;
                    for (const auto& values : xs) {
                        DenseConstView input{n, f, {values.data(), values.size()}};
                        std::fill(y.begin(), y.end(), std::numeric_limits<float>::quiet_NaN());
                        prepared.run(p.graph.view(), input, output, cfg, nullptr);
                        error = std::max(error, b_test::check(y.data(), b_test::oracle(p, input)));
                    }
                    std::vector<double> times;
                    times.reserve(10);
                    for (unsigned repeat = 0; repeat < 15; ++repeat) {
                        const auto& values = xs[repeat % 2];
                        const DenseConstView input{n, f, {values.data(), values.size()}};
                        const auto start = Clock::now();
                        prepared.run(p.graph.view(), input, output, cfg, nullptr);
                        const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
                        if (repeat >= 5) times.push_back(seconds);
                        consume(y);
                    }
                    const auto median = quantile(times, .5);
                    const auto iqr = quantile(times, .75) - quantile(times, .25);
                    for (std::size_t i = 0; i < times.size(); ++i)
                        std::cout << "b-fixed-hubs-v1," << (skewed ? "skewed" : "uniform") << ','
                            << n << ',' << p.graph.values.size() << ',' << f << ',' << variant.name << ','
                            << variant.tile << ',' << i << ',' << 5206811 + (i + 5) % 2 << ','
                            << times[i] << ',' << median << ',' << iqr << ',' << error << '\n';
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}

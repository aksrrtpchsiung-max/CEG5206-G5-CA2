#include "checks.hpp"
#include <algorithm>
#include <chrono>
#include <random>
#include <unordered_set>

namespace ceg5206::a {
namespace {
using Clock = std::chrono::steady_clock;

std::uint64_t draw(std::mt19937_64& rng, std::uint64_t bound) {
    const auto threshold = (std::uint64_t{0} - bound) % bound;
    std::uint64_t u;
    do { u = rng(); } while (u < threshold);
    return u % bound;
}

std::vector<NodeId> sample(std::mt19937_64& rng, std::uint64_t population, std::uint64_t k) {
    std::unordered_set<NodeId> selected;
    selected.reserve(k);
    for (auto j = population - k; j < population; ++j) {
        const auto t = static_cast<NodeId>(draw(rng, j + 1));
        if (!selected.insert(t).second) selected.insert(static_cast<NodeId>(j));
    }
    std::vector<NodeId> ids(selected.begin(), selected.end());
    std::sort(ids.begin(), ids.end());
    return ids;
}

RowOffset share(RowOffset total, std::uint64_t count, std::uint64_t index) {
    return total / count + (index < total % count);
}

std::vector<RowOffset> row_degrees(const GraphConfig& cfg, std::mt19937_64& rng) {
    const auto n = cfg.n, m = cfg.m;
    if (cfg.family == GraphFamily::uniform) {
        std::vector<RowOffset> degrees(n);
        for (std::size_t i = 0; i < n; ++i) degrees[i] = share(m, n, i);
        return degrees;
    }
    if (cfg.family != GraphFamily::skewed || n < 2)
        throw std::invalid_argument("skewed requires n >= 2 and a supported family");
    const auto h = (n + 99) / 100;
    const auto hub_edges = m / 2;
    if (share(hub_edges, h, 0) >= n || share(m - hub_edges, n - h, 0) >= n)
        throw std::invalid_argument("skewed degree exceeds distinct available neighbours");
    const auto hubs = sample(rng, n, h);
    std::vector<RowOffset> degrees(n);
    std::size_t hub_index = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (hub_index < h && hubs[hub_index] == i)
            degrees[i] = share(hub_edges, h, hub_index++);
        else
            degrees[i] = share(m - hub_edges, n - h, i - hub_index);
    }
    return degrees;
}
} // namespace

GeneratedGraph generate_graph(const GraphConfig& cfg) {
    detail::require(detail::nodes(cfg.n));
    if (cfg.m > (cfg.n ? cfg.n * (cfg.n - 1) : 0))
        throw std::invalid_argument("m exceeds directed graph capacity");
    if (cfg.m > detail::max_elements<WeightedEdge>())
        throw std::overflow_error("canonical edge storage size overflow");
    GeneratedGraph result;
    result.config = cfg;
    std::mt19937_64 rng(cfg.graph_seed);
    const auto start = Clock::now();
    const auto degrees = row_degrees(cfg, rng);
    auto& edges = result.oracle_edges;
    edges.reserve(cfg.m);
    for (std::size_t row = 0; row < cfg.n; ++row) {
        const auto neighbours = sample(rng, cfg.n - 1, degrees[row]);
        for (auto id : neighbours) {
            const auto col = static_cast<NodeId>(id + (id >= row));
            edges.push_back({static_cast<NodeId>(row), col, 0.0f});
        }
    }
    const auto sampled = Clock::now();
    std::size_t i = 0;
    for (const auto degree : degrees) {
        if (!degree) continue;
        const auto weight = static_cast<float>(1.0 / static_cast<double>(degree));
        for (RowOffset j = 0; j < degree; ++j) edges[i++].weight = weight;
    }
    const auto weighted = Clock::now();
    result.csr = detail::csr_from_sorted_edges({cfg.n, {edges.data(), edges.size()}});
    const auto built = Clock::now();
    result.timings = {std::chrono::duration<double>(sampled - start).count(),
                      std::chrono::duration<double>(weighted - sampled).count(),
                      std::chrono::duration<double>(built - weighted).count()};
    return result;
}

std::vector<float> generate_features(std::uint64_t n, FeatureCount f, std::uint64_t seed) {
    detail::require(detail::dimensions<float>(n, f));
    std::vector<float> x(n * f);
    std::mt19937_64 rng(seed);
    for (auto& value : x) value = static_cast<float>(rng() >> 40) * 0x1p-23f - 1.0f;
    return x;
}

DegreeStats degree_stats(CsrView g) {
    detail::require(validate_csr(g));
    DegreeStats stats;
    stats.n = g.n;
    stats.m = g.values.size;
    if (!g.n) return stats;
    std::vector<RowOffset> degrees(g.n);
    for (std::size_t i = 0; i < g.n; ++i) {
        degrees[i] = g.row_ptr.data[i + 1] - g.row_ptr.data[i];
        if (!degrees[i]) ++stats.empty_rows;
    }
    std::sort(degrees.begin(), degrees.end());
    stats.min = degrees.front();
    stats.max = degrees.back();
    stats.mean = double(stats.m) / g.n;
    stats.median = 0.5 * (double(degrees[(g.n - 1) / 2]) + double(degrees[g.n / 2]));
    stats.p90 = degrees[(90 * g.n + 99) / 100 - 1];
    stats.p99 = degrees[(99 * g.n + 99) / 100 - 1];
    return stats;
}
} // namespace ceg5206::a

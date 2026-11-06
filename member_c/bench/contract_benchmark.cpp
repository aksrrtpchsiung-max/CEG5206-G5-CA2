#include <ceg5206/contracts.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
using namespace ceg5206;
using Clock = std::chrono::steady_clock;

struct Problem {
    CsrGraph graph;
    std::vector<WeightedEdge> edges;
};

Problem make_problem(std::uint32_t n, std::uint32_t mean_degree, bool skewed) {
    const std::uint64_t m = static_cast<std::uint64_t>(n) * mean_degree;
    std::vector<std::uint32_t> degrees(n, mean_degree);
    if (skewed) {
        const std::uint32_t hubs = (n + 99U) / 100U;
        const std::uint64_t hub_edges = m / 2U;
        const std::uint64_t other_edges = m - hub_edges;
        for (std::uint32_t row = 0; row < n; ++row) {
            if (row < hubs) {
                degrees[row] = static_cast<std::uint32_t>(
                    hub_edges / hubs + (row < hub_edges % hubs));
            } else {
                const auto local = row - hubs;
                const auto count = n - hubs;
                degrees[row] = static_cast<std::uint32_t>(
                    other_edges / count + (local < other_edges % count));
            }
            if (degrees[row] >= n) {
                throw std::invalid_argument("degree exceeds no-self-loop capacity");
            }
        }
    }

    Problem problem;
    problem.graph.n = n;
    problem.graph.row_ptr.push_back(0);
    problem.edges.reserve(static_cast<std::size_t>(m));
    for (NodeId row = 0; row < n; ++row) {
        std::vector<NodeId> columns;
        columns.reserve(degrees[row]);
        for (std::uint32_t k = 0; k < degrees[row]; ++k) {
            columns.push_back(static_cast<NodeId>((row + 1U + k) % n));
        }
        std::sort(columns.begin(), columns.end());
        const float weight = columns.empty() ? 0.0F : 1.0F / columns.size();
        for (NodeId column : columns) {
            problem.graph.col_idx.push_back(column);
            problem.graph.values.push_back(weight);
            problem.edges.push_back({row, column, weight});
        }
        problem.graph.row_ptr.push_back(problem.graph.col_idx.size());
    }
    return problem;
}
std::vector<float> features(std::uint32_t n, FeatureCount f, std::uint64_t seed) {
    std::vector<float> values(static_cast<std::size_t>(n) * f);
    std::uint64_t state = seed;
    for (float& value : values) {
        state ^= state << 13U;
        state ^= state >> 7U;
        state ^= state << 17U;
        value = static_cast<float>(static_cast<std::int32_t>(state % 2001U) - 1000) /
            1000.0F;
    }
    return values;
}

std::vector<double> oracle(const Problem& problem, DenseConstView x) {
    std::vector<double> result(static_cast<std::size_t>(x.n) * x.f, 0.0);
    for (const auto& edge : problem.edges) {
        for (FeatureCount feature = 0; feature < x.f; ++feature) {
            result[static_cast<std::size_t>(edge.row) * x.f + feature] +=
                static_cast<double>(edge.weight) *
                static_cast<double>(x.values.data[
                    static_cast<std::size_t>(edge.col) * x.f + feature]);
        }
    }
    return result;
}

void validate(const std::vector<float>& actual, const std::vector<double>& expected) {
    for (std::size_t index = 0; index < actual.size(); ++index) {
        const double error = std::abs(static_cast<double>(actual[index]) - expected[index]);
        if (!std::isfinite(actual[index]) ||
            error > 1.0e-5 + 1.0e-4 * std::abs(expected[index])) {
            throw std::runtime_error("FP64 validation failed at output " +
                                     std::to_string(index));
        }
    }
}

double quantile(std::vector<double> values, double q) {
    std::sort(values.begin(), values.end());
    const double index = (values.size() - 1U) * q;
    const auto lower = static_cast<std::size_t>(index);
    const auto upper = std::min(lower + 1U, values.size() - 1U);
    return values[lower] + (values[upper] - values[lower]) * (index - lower);
}

struct Variant {
    const char* name;
    PrepareFn prepare;
    std::uint32_t threads;
};

void benchmark_case(std::ofstream& output, const char* family, Problem& problem,
                    FeatureCount f, const Variant& variant) {
    const RunConfig config{variant.threads, 32, 128};
    auto prepared = variant.prepare(problem.graph.view(), f, config);
    auto x0_values = features(static_cast<std::uint32_t>(problem.graph.n), f, 5206101);
    auto x1_values = features(static_cast<std::uint32_t>(problem.graph.n), f, 5206102);
    const std::vector<DenseConstView> inputs{
        {problem.graph.n, f, {x0_values.data(), x0_values.size()}},
        {problem.graph.n, f, {x1_values.data(), x1_values.size()}},
    };
    std::vector<float> y(static_cast<std::size_t>(problem.graph.n) * f);
    const DenseView y_view{problem.graph.n, f, {y.data(), y.size()}};

    for (std::size_t input = 0; input < inputs.size(); ++input) {
        const auto expected = oracle(problem, inputs[input]);
        std::fill(y.begin(), y.end(), std::numeric_limits<float>::quiet_NaN());
        prepared.run(problem.graph.view(), inputs[input], y_view, config,
                     prepared.state.get());
        validate(y, expected);
    }
    for (int warmup = 0; warmup < 5; ++warmup) {
        prepared.run(problem.graph.view(), inputs[warmup % 2], y_view, config,
                     prepared.state.get());
    }
    const auto probe_start = Clock::now();
    prepared.run(problem.graph.view(), inputs[0], y_view, config,
                 prepared.state.get());
    const double probe_seconds = std::chrono::duration<double>(
        Clock::now() - probe_start).count();
    const auto batch_calls = static_cast<unsigned>(std::clamp(
        std::ceil(0.02 / std::max(probe_seconds, 1.0e-9)), 1.0, 100.0));
    std::vector<double> samples;
    samples.reserve(10);
    double checksum = 0.0;
    for (int repetition = 0; repetition < 10; ++repetition) {
        const auto start = Clock::now();
        for (unsigned batch = 0; batch < batch_calls; ++batch) {
            prepared.run(problem.graph.view(), inputs[(repetition + batch) % 2],
                         y_view, config, prepared.state.get());
        }
        const auto stop = Clock::now();
        samples.push_back(
            std::chrono::duration<double>(stop - start).count() / batch_calls);
        for (float value : y) {
            checksum += value;
        }
    }
    const double median = quantile(samples, 0.5);
    const double iqr = quantile(samples, 0.75) - quantile(samples, 0.25);
    const double operations = 2.0 * problem.graph.values.size() * f;
    output << "portable_x64_validation_only," << family << ',' << problem.graph.n << ','
           << problem.graph.values.size() / problem.graph.n << ',' << f << ','
           << variant.name << ',' << variant.threads << ",32,128," << std::setprecision(17)
           << batch_calls << ',' << median << ',' << iqr << ','
           << operations / (1.0e9 * median) << ','
           << prepared.workspace.descriptors << ',' << prepared.workspace.partials << ','
           << prepared.workspace.other << ',' << checksum << ",true\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::invalid_argument("usage: c_contract_benchmark OUTPUT.csv");
        }
        std::ofstream output(argv[1], std::ios::trunc);
        if (!output) {
            throw std::runtime_error("cannot open output CSV");
        }
        output << "environment,family,n,mean_degree,f,variant,threads,Tf,Te,"
                  "batch_calls,median_seconds,iqr_seconds,nominal_gflops,descriptor_bytes,"
                  "partial_bytes,other_workspace_bytes,checksum,fp64_validation\n";
        const auto hardware = std::max(1U, std::thread::hardware_concurrency());
        const auto parallel_threads = std::min(4U, hardware);
        const std::vector<Variant> variants{
            {"b_simd_abi_portable_shim", b::prepare_simd, 1},
            {"row_static", c::prepare_static, 1},
            {"row_dynamic", c::prepare_dynamic, 1},
            {"split_row", c::prepare_split, 1},
            {"row_static", c::prepare_static, parallel_threads},
            {"row_dynamic", c::prepare_dynamic, parallel_threads},
            {"split_row", c::prepare_split, parallel_threads},
        };
        for (bool skewed : {false, true}) {
            auto problem = make_problem(8192, 32, skewed);
            for (FeatureCount f : {16U, 64U}) {
                for (const auto& variant : variants) {
                    benchmark_case(output, skewed ? "skewed" : "uniform",
                                   problem, f, variant);
                }
            }
        }
        std::cout << "Wrote preliminary contract benchmark to " << argv[1] << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}

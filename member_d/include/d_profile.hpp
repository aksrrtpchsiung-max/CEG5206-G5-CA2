#pragma once
#include <chrono>
#include <cstdint>
#include <vector>
#include <omp.h>
namespace member_d::profile {
using Clock = std::chrono::steady_clock;
struct Snapshot {
    double initialization = 0, aggregation = 0, merge = 0;
    std::vector<std::uint64_t> worker_edges;
};
struct alignas(128) Counter { std::uint64_t value = 0; };
inline std::vector<Counter> counters;
inline Snapshot data;
inline Clock::time_point start;
inline void reset(unsigned workers) { data = {}; data.worker_edges.assign(workers, 0); counters.assign(workers, {}); }
inline void begin() { start = Clock::now(); }
inline void end(double& value) { value = std::chrono::duration<double>(Clock::now() - start).count();
    for (std::size_t i = 0; i < counters.size(); ++i) data.worker_edges[i] = counters[i].value;
}
inline void edges(std::uint64_t count) { counters[omp_get_thread_num()].value += count; }
}

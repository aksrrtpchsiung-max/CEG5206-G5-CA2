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
inline Snapshot data;
inline Clock::time_point start;
inline void reset(unsigned workers) { data = {}; data.worker_edges.assign(workers, 0); }
inline void begin() { start = Clock::now(); }
inline void end(double& value) { value = std::chrono::duration<double>(Clock::now() - start).count(); }
inline void edges(std::uint64_t count) { data.worker_edges[omp_get_thread_num()] += count; }
}

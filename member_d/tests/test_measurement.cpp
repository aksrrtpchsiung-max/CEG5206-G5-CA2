#include "measurement.hpp"
#include <iostream>

namespace {
unsigned calls = 0;
void fake(ceg5206::CsrView, ceg5206::DenseConstView x, ceg5206::DenseView y,
          const ceg5206::RunConfig&, ceg5206::KernelState*) {
    ++calls;
    y.values.data[0] = x.values.data[0];
}
void check(bool condition) {
    if (!condition) throw std::runtime_error("measurement test failed");
}
}
int main() {
    // A test-only fake verifies the harness; never registered as an algorithm.
    ceg5206::PreparedKernel kernel;
    kernel.run = fake;
    float a = 2, b = 5, out = -1;
    const std::vector<ceg5206::DenseConstView> inputs{{1, 1, {&a, 1}}, {1, 1, {&b, 1}}};
    auto samples = member_d::measure(kernel, {}, inputs, {1, 1, {&out, 1}}, {}, 5, 10);
    check(calls == 15 && samples.seconds.size() == 10 && samples.checksum == 35 && out == 5);
    calls = 0;
    auto reuse = member_d::measure(kernel, {}, inputs, {1, 1, {&out, 1}}, {}, 5, 10, 3, 0);
    check(calls == 35 && reuse.reuse_calls == 3 && reuse.batch_groups == 1 && reuse.checksum == 35);
    for (std::size_t i = 0; i < reuse.seconds.size(); ++i)
        check(std::abs(reuse.sequence_seconds[i] - 3 * reuse.seconds[i]) < 1e-12);
    check(member_d::quantile({1, 2, 3, 4}, .5) == 2.5);
    check(member_d::quantile({1, 2, 3, 4}, .75) - member_d::quantile({1, 2, 3, 4}, .25) == 1.5);
    bool overflow = false;
    try { (void)member_d::mul(UINT64_MAX, 2); } catch (const std::overflow_error&) { overflow = true; }
    check(overflow);
    bool missing = false;
    kernel.run = nullptr;
    try { (void)member_d::measure(kernel, {}, inputs, {1, 1, {&out, 1}}, {}, 5, 10); }
    catch (const std::invalid_argument&) { missing = true; }
    check(missing);
    std::cout << "measurement tests passed (no performance evidence)\n";
}

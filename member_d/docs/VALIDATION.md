# v0.1 Harness Validation Record

Date: 2026-11-05. Base: A's commit
`0eaeb1518228985b134c6220c41252c52f1f537c`.
Validation platform: macOS arm64, Apple Clang 17.0.0, Python 3.9, and CMake 4.4.4
installed in a temporary directory. These checks validate the harness, not project
performance; no algorithm speedup is claimed.

Completed checks:

- Direct Clang build of the default benchmark, without warnings. All five
  variants report unavailable.
- C++ syntax checking with A/B/C execution paths enabled. This is not a link test
  against actual member implementations.
- CMake Release configure/build and CTest: one measurement test passed.
- Measurement tests: warmup/timed call counts, alternating inputs, observable
  outputs, median/IQR, overflow, and rejection of a missing kernel.
- Python unittest: seven tests passed, none skipped. Coverage includes deterministic
  expansion of 120 formal cases, tuning/reuse plans, impossible skewed graphs,
  scaling thread lists, no measurements for missing implementations, matching
  baseline summaries, and end-to-end output through a test-only A adapter.
- Five configuration plans: smoke=6, main=120, tuning=12, reuse=30, scaling example=44 cases.

The end-to-end adapter accepts only a fixed two-node test input. It lives in
tests/fake_a.cpp and requires D_TEST_ONLY to compile. Tests write to temporary
directories and remove their outputs; test timings are not published as experiment
results. The production build neither registers nor links this adapter.

Pending actual algorithm delivery: A's inputs and independent oracle, B's SIMD
instructions and tails, C's chunks/races/merging, actual workspace and temporary
peaks, final ISA/OpenMP toolchain, complete experiments, and profiling.

# Native Integration and Handoff

## Source versions and interfaces

A 43842aa supplies actual generators, validators, CSR baseline, independent FP64
oracle, comparator and chunk audit. B 2c8f04f supplies prepare_simd and
row_range_simd. C 8a06e1b supplies prepare_static, prepare_dynamic, prepare_split
and chunk_plan_view. D uses the unchanged public v0.1 header. Native integration
is complete for the measured campaign; final team approval of the specification
is a separate team decision.

A scalar remains a separate translation unit with automatic vectorization off.
All numerical sources use -O3, no fast math, and FP contraction off. B's ablation
source edge_major.cpp is a labeled additional control, not a replacement kernel.

## Execution contract

D owns two X arrays, Y and reference storage. CSR/X are read-only during run.
Each prepared state binds graph storage, n, f and all RunConfig fields. Changing
any of these requires preparation again. D destroys prepared state before graph
storage. Hot-path calls overwrite Y and reset all necessary per-call state.

D repeats independent FP64 checks on alternating inputs with NaN-prefilled Y,
audits every split plan, releases oracle/reference storage before timing, and
records input digests to verify that variants actually receive the same data.
Production builds never link tests/fake_a.cpp, B's test fixture, or C's portable shim.

## Diagnostic hooks

D adds only compile-guarded CEG5206_D_DIAGNOSTICS hooks to C's production adapter.
They count actual worker edges and measure initialization, aggregation and merge
regions. They are absent from benchmark and enabled only in benchmark_profile.
Diagnostic globals are for sequential harness calls, not concurrent invocation of
multiple prepared operators. Diagnostic instrumentation can perturb runtime and
must not be used for primary speedups.

## Validation and reproducibility

Native A+B+C release and ASan/UBSan suites pass. B's standalone optimized,
sanitizer, assembly and D handoff checks pass. C's own tests link the real NEON
source. D measurement, allocation and entry tests pass. Logs are retained under
evidence/apple-m4. Existing A cross-review contains a separate x86 TSan/Archer
check; no native M4 TSan result is claimed.

Each phase records the actual binary hash, source revision and dirty-source patch.
The report's evidence index hashes all campaign artifacts. Build and campaign
scripts reproduce the integration and measurement protocol. Native results apply
to this machine and recorded OS/thread/power policies only.

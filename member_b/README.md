# Member B: feature tiling and SIMD

Implements B's proposal responsibility: feature tiling, SIMD, safe tails, and
evidence for profiling/cache interpretation. Both public entry points use A's
unchanged `ceg5206/contracts.hpp` v0.1:

- `b::prepare_simd`: complete single-thread weighted CSR aggregation, overwriting Y.
- `b::row_range_simd`: allocation-free row/chunk contribution, overwriting exactly f elements.

The implementation targets **AArch64 NEON (4 FP32 lanes)**. Apple Silicon is the
verified platform; other ISAs fail at build time. There is no scalar substitute
registered as `simd`. AArch64 is B's implemented platform choice, not a claim that
the whole team has approved its final experiment machine.

## Build and verify

Run from the repository root with Clang, C++17 and Python 3:

```bash
python3 member_b/scripts/verify.py
# Includes separate ThreadSanitizer diagnostics as well:
python3 member_b/scripts/verify.py --tsan
```

This builds/runs optimized and ASan/UBSan tests, generates assembly and checks its
SIMD/FMA policy, builds the local attribution probe, and links actual B into D's
runner with a guarded test-only A adapter. No CMake, OpenMP or Python packages are
needed. Generated binaries, assembly and fixture headers stay in ignored `build/`.

The CMake route builds a reusable `ceg5206_b` static library:

```bash
cmake -S member_b -B member_b/build-release -DCMAKE_BUILD_TYPE=Release
cmake --build member_b/build-release -j
ctest --test-dir member_b/build-release --output-on-failure

cmake -S member_b -B member_b/build-asan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DB_SANITIZE=ON
cmake --build member_b/build-asan -j
ctest --test-dir member_b/build-asan --output-on-failure
```

`BUILD_TESTING=OFF` removes Python and thread test dependencies;
`B_BUILD_PROBE=OFF` omits the diagnostic executable. Production B needs only the
shared header, a C++17 compiler and AArch64 NEON. With CMake's sanitizer option,
consume the `ceg5206_b_policy` target as well if linking a custom diagnostic binary.

## Delivered behavior

- FP32 multiplication/accumulation in CSR edge order; separate multiply and add.
- Stored weights used unchanged; no implicit normalization or self-loops.
- Feature tiles of 16/32/64 use 4/8/16 vector accumulators. Other positive sizes
  use an output-backed SIMD tile, without heap allocation or a tile-size limit.
- Exact tails: no padding or 16-byte alignment requirement. Widths below four
  naturally use scalar remainder arithmetic within the same SIMD algorithm.
- Empty rows/ranges produce zero; full calls overwrite every Y element even after
  NaN prefilling. No dependence on old output or the previous X address.
- No threads, shared mutable state or retained workspace. C can call row ranges
  concurrently when each call owns a disjoint output/partial span.
- `prepare_simd` rejects invalid configuration/storage shape and unaddressable
  dense sizes. As agreed, A/caller validates full CSR, X, aliasing and ranges before
  the hot path. Invalid hot-path inputs are not supported.

## Files and evidence

| File | Purpose |
|---|---|
| `src/simd.cpp` | Entire production B implementation; the only B source D needs |
| `tests/test_simd.cpp` | FP64, tails, overwrite, range-split, threading and protected-page tests |
| `tests/fixture_header.py` | Reads A's actual JSON fixture; no private replacement contract |
| `tests/d_fixture_a.cpp` | Explicitly guarded B/D link test adapter, never a production A replacement |
| `bench/` | Separate scalar and edge-major SIMD controls plus local diagnostic probe |
| `scripts/verify.py` | Reproducible Clang verification, including D integration |
| `scripts/probe.py` | Raw local timing CSV, summary and provenance manifest |
| [Handoff](docs/HANDOFF.md) | C/D integration and lifecycle details |
| [Design and attribution](docs/DESIGN.md) | Loop structure, traffic model, assembly and measured comparisons |
| [Validation](docs/VALIDATION.md) | Checks actually executed and remaining team dependencies |
| `evidence/apple-m5-n8192-d8/` | 300 raw local diagnostic timings, with explicit limitations |

To reproduce local attribution (choose a new output directory each run):

```bash
python3 member_b/scripts/verify.py
python3 member_b/scripts/probe.py --nodes 8192 --degree 8 --out member_b/out/probe-01
```

These measurements use B's labelled diagnostic generator and controls. They do
not replace A's implementation, C's scheduling, D's formal 24-workload experiment,
or the team's split-row parameter selection. The local branch includes A/D source
history so the handoff can be built immediately; it has not been pushed.

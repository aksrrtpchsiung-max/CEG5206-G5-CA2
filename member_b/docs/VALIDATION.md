# B validation and contribution record

Executed on 5 November 2026 (Asia/Singapore): Apple M5 / macOS 26.6.2 arm64,
Apple Clang 21.0.0, Python 3.9 for the direct verification script, CMake 4.4.4
temporarily installed under `/tmp`. CMake selected Python 3.14.6 for fixture
generation. No remote branch or pull request was created.

## Completed checks

| Check | Result |
|---|---|
| Clang `-O3`, strict warnings including `-Werror` | Passed |
| Optimized correctness executable | 17,709 checks passed |
| Separate AddressSanitizer + UndefinedBehaviorSanitizer build | 17,709 checks passed; no diagnostic |
| Separate ThreadSanitizer build | 17,709 checks passed; no diagnostic |
| CMake Release build + CTest | 1/1 passed |
| CMake RelWithDebInfo with `B_SANITIZE=ON` + CTest | 1/1 passed |
| Generated assembly inspection | NEON vector multiply/add present; no fused arithmetic; scalar control unvectorized |
| Actual B linked with D entry/export pipeline | Six cases: scalar control/B at f=1,17,65; two inputs per case validated |
| Existing D CMake/measurement test | 1/1 passed |
| Existing D Python tests with its original fixture binary | 7/7 passed, none skipped |
| Local attribution probe | 30 validated variant/workload combinations, 300 raw timed calls saved |

All B correctness builds reported:

```text
PASS 17709 checks; max_scaled_error=0.0107248; tolerance=1e-5+1e-4*abs(reference)
```

The scaled-error pass threshold is 1. No tolerance was relaxed. The local timing
probe independently validated every variant on both X inputs; its maximum scaled
error was 0.006065515324108606. Times from sanitizer/fixture tests are not used as
performance evidence.

## Coverage

- A's actual `contract_v0_1.json` is parsed at build time into a test-only header.
  Its hand-written Y and partials check the independent FP64 edge-list calculation,
  the full B operator and the supplied chunk decomposition.
- f=1..70,127,128,129,255,256,257 and Tf=1,2,3,4,7,15,16,17,31,32,33,63,64,65,257,
  UINT32_MAX, including f<Tf and non-multiples of both four and Tf.
- Empty graph with null zero-length X/Y, all-empty graph, isolated rows, a single
  edge, explicit self-loops, signed/zero weights, 4096-degree normalized hub and
  twelve separately generated signed graph topologies.
- Three successive full calls using X0/X1/X0 and NaN-prefilled Y, checking input
  immutability, zero retained workspace and front/back output canaries.
- Full rows at all split points, empty prefixes/suffixes, and deterministic
  FP32 sums of partial contributions checked against FP64.
- Four workers writing disjoint rows repeatedly, and four concurrent chunks of
  the same row writing private partial spans; separate TSan run.
- Natural-only alignment: X/Y shifted by one float, unpadded odd row strides.
  Protected memory pages immediately after input/output for every f=1..131 and
  nine tile widths catch over-reads and over-writes in Release as well as diagnostics.
- Rejection of unsupported thread counts, f/Tf/Te=0, null/wrong CSR span shape,
  node-limit overflow and unaddressable dense products.

These tests use independently retained edge records before CSR construction.
The test oracle uses double(stored FP32 weight) and double(stored FP32 X), not
recomputed ideal weights. Test utilities are private to B tests/probes, do not
define A production symbols, and are not part of the B library. Only the explicitly
guarded D link adapter supplies temporary A names for that integration executable.

## Reproduction

From the repository root:

```bash
python3 member_b/scripts/verify.py --tsan
cmake -S member_b -B member_b/build-release -DCMAKE_BUILD_TYPE=Release
cmake --build member_b/build-release -j
ctest --test-dir member_b/build-release --output-on-failure
cmake -S member_b -B member_b/build-asan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DB_SANITIZE=ON
cmake --build member_b/build-asan -j
ctest --test-dir member_b/build-asan --output-on-failure
python3 member_b/scripts/probe.py --out member_b/out/reproduction --nodes 8192 --degree 8
```

If CMake is absent, the first command performs all core B checks and the D link
test by direct Clang invocation. The CMake build is an independently verified
alternative. `verify.py` leaves assembly and binaries in ignored `member_b/build/`;
`probe.py` refuses to overwrite an existing result directory and labels failures.

## B contribution mapped to the proposal

| Responsibility | Delivered artifact/evidence |
|---|---|
| Feature tiling | Register tiles 16/32/64 and arbitrary positive cache-backed tiles |
| SIMD implementation | Explicit NEON contiguous-feature arithmetic and verified assembly |
| Tail handling | Unaligned-safe bounded loads, scalar remainders, guard-page/ASan checks |
| B-to-C integration | Exact shared `RowRangeFn` semantics, partial/parallel tests and handoff |
| B-to-D integration | `prepare_simd`, zero workspace, common FP flags, real D calling/export tests |
| Profiling/cache cross-review | Traffic/accumulator model, assembly review, scalar/edge-major/tiled controls and raw local timings |
| Reproducibility/report contribution | Build files, scripts, validation record, design/results text and source/binary provenance |

## Limits

The inspected remote branches provide A's declarations and D's harness, not actual
A/C algorithms. The D fixture proves B linkage/lifecycle/export behavior only.
Actual A FP64/generator checks, C schedules/chunk audit, formal parameter freezing,
the complete workload matrix and hardware-counter profiling belong to subsequent
team integration. Unsupported x86 ISA builds fail explicitly; GCC/Linux were not
executed. Local measurements use a fixed-order, OS-scheduled warm-start diagnostic
protocol and cannot establish a universal speedup or final tile choice.

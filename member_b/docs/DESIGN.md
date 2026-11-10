# B design and attribution evidence

## Computation and loop ordering

The operator is exactly weighted CSR SpMM: `Y[i,r] = sum_e values[e] * X[col[e],r]`.
Each feature accumulates in FP32, in CSR edge order. No horizontal reduction, graph
mutation or extra normalization is introduced. SIMD lanes span contiguous feature
positions, never irregular neighbor IDs. Stored FP32 weights are used unchanged.

The local scalar/edge-major controls run row -> edge -> feature. Each edge updates
the output row again. The B kernel runs row -> feature tile -> edge -> SIMD vectors.
For widths 16/32/64, accumulators start at zero, remain live across all edges and
are stored once at tile completion. For arbitrary widths, the tile is backed by
the output span, initialized within the call and updated with NEON plus scalar tails.
This avoids an unbounded variable-size stack buffer and preserves any positive Tf.

The tile boundary is `min(Tf, f-feature)`; feature advances by that bounded width,
avoiding overflow even for `Tf=UINT32_MAX`. Four-lane accesses require four valid
elements. One to three remaining features use scalar arithmetic; no masked load,
padding, speculative out-of-range intrinsic or 16-byte alignment is assumed.
An empty range directly zeros all f outputs, without dereferencing edge arrays.

## Accumulator storage and expected access cost

For a full register tile, accumulator payload is `4*Tf` bytes: 64, 128, or 256
bytes for Tf=16,32,64, corresponding to 4,8,16 NEON registers. Neighbor X data and
CSR metadata are separate. The cache-backed general path has a logical live tile
of the same size, stored in existing Y/partial memory; it is not guaranteed to fit
in cache for an arbitrary large user tile.

Ignoring scalar control/address instructions and cache effects, for nonempty rows:

| Logical accesses | Edge-major | Full register-tile path |
|---|---:|---:|
| Neighbor feature reads | `4*m*f` bytes | `4*m*f` bytes |
| Weight and column reads | `8*m` bytes | `8*m*ceil(f/Tf)` bytes |
| Output initial/store traffic | zeroing plus repeated updates | `4*n*f` final bytes, including empty-row stores |
| Repeated accumulator reads/writes | about `8*m*f` bytes in Y | registers, except generic/tail tiles |

The output traffic estimates describe source-level operations, not measured DRAM
traffic; compilers and caches can eliminate or combine accesses. Generic/tail tiles
can still read/write output on each edge. More tiling can improve output reuse yet
revisit metadata more often. An X row of f=256 spans 1024 bytes even if a tile only
uses 64 bytes. Cache-line fetches, neighbor locality, degree skew and cache capacity
therefore matter independently of nominal allocated bytes.

There is no retained workspace, heap scratch or per-call allocation. The compiler
can use a small stack frame for saved registers/address bookkeeping. This is not
reported as an owned workspace array under contract §7.2; it is also not a claim
that the function has literally zero stack usage.

## Assembly inspection

Built with Apple Clang 21.0.0, `-O3 -fno-fast-math -ffp-contract=off`, on Apple M5.
`verify.py` saves `build/simd.s` and `build/scalar.s`, requires packed NEON arithmetic
in B, rejects fused arithmetic, and rejects packed arithmetic in the scalar control.
Manual inspection of the generated inner loops additionally found:

- 16-feature path: accumulators v0-v3, separate `fmul.4s` and `fadd.4s`.
- 32-feature path: accumulators v0-v7; no accumulator spill/reload inside the edge loop.
- 64-feature path: accumulators v0-v7 and v16-v23; no accumulator spill/reload inside the edge loop.
- X uses vector loads, weights are broadcast, and output vector stores occur after
  the register-tile edge loop. General-path output loads/stores are expected.
- The row-range function has a 208-byte stack frame in this build for integer
  registers/bookkeeping. This does not contradict the inspected SIMD register residence.

This is compiler/build-specific evidence, not a guarantee for another toolchain.
Assembly confirms instruction selection; it does not prove cache-hit rates or
memory bandwidth saturation. No hardware performance counters were collected.

## Local experiment, 5 November 2026

The committed `evidence/apple-m5-n8192-d8/` contains raw calls, summary CSV and a
manifest with CPU/compiler, binary/source hashes and execution policy. Workloads:
n=8192, m=65536, f=16/64/256, uniform and skewed degree. The local generator
`b-fixed-hubs-v1` fixes the first ceil(0.01*n) rows as hubs receiving half the edges;
neighbors are sampled without replacement and sorted. This is deliberately not
labelled as A's random-hub `a-v0.1` generator.

All variants use the same graph and two feature arrays. We validate each against
the independent FP64 edge list, run five warmups and ten timed full calls, and scan
Y after each call. Generation, allocation, prepare and checking are outside timing;
all output initialization is inside. Scalar SIMD is disabled only for its own
translation unit; all arithmetic has the same non-FMA policy.

Measured median microseconds (one local run):

| Family | f | Scalar control | Edge-major NEON | Tiled 16 | Tiled 32 | Tiled 64 |
|---|---:|---:|---:|---:|---:|---:|
| uniform | 16 | 890.71 | 266.60 | 126.35 | 128.33 | 117.46 |
| uniform | 64 | 1493.06 | 530.10 | 292.77 | 283.81 | 265.46 |
| uniform | 256 | 5294.83 | 1454.29 | 1187.23 | 1045.54 | 954.50 |
| skewed | 16 | 385.81 | 165.00 | 87.79 | 81.13 | 81.33 |
| skewed | 64 | 1334.10 | 498.48 | 294.79 | 255.58 | 262.75 |
| skewed | 256 | 5681.69 | 1741.88 | 1232.19 | 1061.54 | 983.56 |

Maximum scaled FP64 error across these measurements was 0.006066, below the pass
threshold 1. In this run, register tiling reduced median time relative to the
edge-major SIMD control on each workload. At f=256, larger tiles performed better,
consistent with fewer metadata traversals, but counters would be needed to attribute
the cause conclusively. This is not evidence about C's scheduling or split-row gains.

At f=16 all Tf=16/32/64 requests take the same effective 16-wide path. Differences
between those timings therefore demonstrate measurement/environment variability,
not a tile-size effect. Variant order was fixed; affinity and CPU frequency were
OS-controlled, with no process-isolated repetitions or hardware counters. Reported
L1D=64 KiB and L2=6 MiB are host sysctl values, not a complete heterogeneous cache
topology description. Do not infer cache residence solely from these sizes.

## B cross-review conclusions

For D's eventual profiling, separate SIMD instruction effects, accumulator reuse,
metadata rereads and scheduling/merge overhead. Keep full-call time as the main
measure and report cache/per-worker evidence separately. Retained workspace bytes
must not be presented as DRAM traffic. A short-call timing failure requires batching,
not artificial work inside a production kernel. Recheck assembly under the actual
final compiler/flags and validate both X arrays after any numeric-policy change.

This completes B's local implementation/attribution deliverable. The team's final
tuning, formal workload matrix and profiler interpretation remain dependent on
actual A/C implementations and D's full experiment pipeline.

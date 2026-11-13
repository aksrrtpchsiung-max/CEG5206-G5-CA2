# B handoff to A / C / D

Base: A contract commit `0eaeb1518228985b134c6220c41252c52f1f537c`, inherited through
D branch commit `e6921eb`. Shared declarations and other members' source are
unchanged. The contract documents describe a proposal; this implementation follows
it without purporting to approve it on behalf of other members.

## Production source and flags

Compile `member_b/src/simd.cpp` and include `member_a/include`. Alternatively link
CMake target `ceg5206_b`. There are no production OpenMP, pthread, Python, external
library or A implementation-symbol dependencies.

Verified ISA/toolchain: Apple M5, AArch64 NEON, Apple Clang 21.0.0, C++17.
Use `-O3 -fno-fast-math -ffp-contract=off` for every numerical implementation.
Only A's scalar translation unit additionally disables vectorization. B's Clang
pragmas also disable contraction/reassociation, and `__FAST_MATH__` is rejected.
Do not add `-Ofast`, reassociation or FMA as a private B optimization. GCC CMake
flags are provided, but GCC/Linux execution has not been verified here.

## C: complete rows and chunks

```cpp
ceg5206::b::row_range_simd(graph, x, row, edge_begin, edge_end,
                          {partial_or_y, x.f}, config.tile_features);
```

Preconditions from the shared contract:

- `edge_begin <= edge_end`, both within the indicated row; 64-bit offsets stay 64-bit.
- The output span has exactly f valid FP32 elements and does not overlap any input.
- The graph/X are valid and read-only; f and tile_features are positive.
- Each worker owns disjoint output storage. The primitive does no synchronization.

Every call overwrites its span, including empty ranges. Split chunks compute
independent partials; C synchronizes and merges in descriptor order as specified
by A. C must not use one shared output row for simultaneous chunks. There are no
hidden thread-local/static accumulators, allocations or mutable kernel globals.

Tests exercise every split point of small signed rows, including empty prefix and
suffix, and compare FP32 merged partials against an independent FP64 edge-list
oracle. Separate concurrent tests cover disjoint full rows and four chunks of one
row. This verifies B's primitive, not C's future descriptor construction or merge.

## D: complete operator

```cpp
auto prepared = ceg5206::b::prepare_simd(graph, f, config);
prepared.run(graph, x, y, config, prepared.state.get());
```

The factory requires threads=1 and all size/config parameters positive. It checks
the node limit, dense byte overflow and basic CSR span shape. Full data validation
remains the caller's responsibility, as in the shared lifecycle. Preparation and
run allocate no heap memory; `state` is null and every workspace field is zero.

Although B is stateless, obey the shared prepare binding: re-prepare when graph,
f or any RunConfig field changes. X/Y addresses may change with matching shape.
The implementation does not retain pointers or detect invalid hot-path reuse.
All output zeroing/initialization is inside run. `max_chunk_edges` has no effect on
B's arithmetic but must remain positive. Y cannot alias X/CSR.

The integrated build links the actual A, B and C libraries:

```bash
bash member_d/scripts/build_team.sh
python3 member_d/scripts/run.py list
```

Test fixtures are only linked into explicitly named test executables.

## D integration evidence and review feedback

`verify.py` links B into D's actual benchmark executable and drives D's Python
entry/export pipeline. A compile-guarded, fixed 1024-node cycle supplies missing
A symbols; tests cover f=1/17/65, scalar control and B, two X seeds, repeated
NaN-prefilled validation, five warmups and ten calls. Temporary outputs are deleted.
This establishes linkage and calling/serialization semantics, not real A validation
or representative performance. The scalar control is compiled without SIMD.

D's original two-node fixture exposes a real timer limit for B: a complete call
can round to zero on this host. D correctly rejects that duration. The B test uses
a larger fixture without altering D's timer or adding delays to production B.
Formal very-small-call experiments still need D's documented batching support.

The completed native campaign is archived under `member_d/evidence/apple-m4`.
B's local tile results remain independent diagnostics.

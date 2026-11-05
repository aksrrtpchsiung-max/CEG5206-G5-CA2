# D Integration and Interface Review

Status: D's implementation plan and review feedback, pending A/B/C confirmation.
This work follows A's interface proposal v0.1 without modifying the public header.

## Implemented against the proposal

- Use CsrView / DenseView and the shared RunConfig without new algorithm signatures.
- Call each factory's PreparedKernel; time prepare separately and run as a complete call.
- D owns two X arrays, Y, and the validation reference. Prepared state is destroyed
  before the graph storage.
- Perform FP64 validation and NaN output prefilling outside timed calls. Alternate
  both inputs and repeat validation to check overwrite semantics.
- Prepare again whenever graph, f, threads, tile, or chunk changes.
- Disable fast-math and FMA contraction for all variants. Disable vectorization
  only in the scalar translation unit.
- Account for descriptors, partials, and other workspace separately, using vector
  capacity. Unknown or unavailable variants fail without fallback.

## Items requiring team confirmation or additional evidence

| Item | Reviewers | Current proposal |
|---|---|---|
| Public v0.1 declarations, Y overwrite semantics, prepare lifetime | All | Share A's original header; jointly version any changes |
| A source files and build target | A/D | Separate scalar translation unit; provide remaining source paths, dependencies, and validation commands |
| SIMD ISA, FMA policy, compiler flags | B/D | One numerical policy across variants, safe tails, no silent hot-path fallback |
| B full operator and row-range kernel | B/C/D | Deliver both; C reuses the shared row-range interface |
| Static/dynamic/split sources, threading library, chunk view | C/D | Integrate each variant when ready; simultaneous delivery is unnecessary |
| Seeds, generator_version, CSV, digest algorithm | A/D | Follow A's proposal; input digest is FNV-1a64 native-v1; manifest records binary SHA-256 |
| Workspace capacity and temporary prepare peaks | A/C/D | Current interface reports retained workspace; separately provide construction/prepare peak evidence |
| Phase timings and per-worker edges/time | C/D | Separate diagnostic entry or sidecar; avoid atomic logging in formal hot paths; agree the interface later |
| Warm-start protocol and very short calls | All | Currently one full call followed by a Y scan; add batching before drawing conclusions from calls too short to measure reliably |

The input digest hashes native bytes of row_ptr, col_idx, values, X0, and X1 in that
order. It checks input consistency on the same platform and configuration. It is
not a portable cross-endian serialization or a cryptographic hash. Each case JSON
stores its digest.

## A: first handoff acceptance

Provide linkable generate_graph / generate_features, validate_csr / validate_problem,
prepare_scalar, oracle_fp64, check_close, and degree_stats. Compile scalar separately.
Validate A's fixed handoff fixture and edge cases before D's smoke run. The generator
must follow agreed random-sequence and stored-weight rules. Do not substitute an
edge list decoded from CSR for independent oracle input.

## B: first handoff acceptance

Provide prepare_simd and row_range_simd with ISA and compiler flags. Independently
check f=1/17/65, empty rows, and signed weights. Inputs guarantee only natural
alignment, with no padding; tails must stay within bounds. The complete SIMD
operator accepts threads=1 only.

## C: first handoff acceptance

Deliver prepare_static / prepare_dynamic / prepare_split progressively, together
with chunk_plan_view and threading build dependencies. Split-row must pass A's
chunk audit, FP64 numerical checking, and separate race diagnostics; none replaces
the others. Do not allocate scratch or rebuild descriptors inside run. Necessary
clearing and all merging must finish before run returns. Document actual edge
assignment, merge order, workspace capacity bytes, and temporary peak memory.

## Current scope limits

v0.1 implements the integration harness and smoke/single/scaling execution paths.
It has not been linked against actual A/B/C implementations. Tuning and reuse only
expand plans, and execution explicitly rejects them. The next milestone adds
geometric-mean parameter selection, freeze records, reuse total-time measurements,
and their repetition protocols. Formal profiling, plots, and the final report
follow algorithm integration. Each member must provide their own confirmation;
this record does not approve the proposal on anyone else's behalf.

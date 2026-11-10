# Preliminary x64 contract-integration results

These results validate the C scheduling and measurement path on the available
Windows AMD64 workstation. They are **not** Member B's SIMD results: B's production
source requires AArch64 NEON, so this run used `tests/portable_b_shim.cpp`, which
implements the same overwrite ABI with a scalar feature loop.

Configuration:

- MSVC 2022 17.14, C++17, `/O2 /openmp /fp:strict`;
- `n=8192`, mean degree `32`, uniform and controlled-skew graphs;
- `f=16,64`, `Tf=32`, `Te=128`;
- five warm-up calls, ten measured batches, approximately 20 ms per batch;
- two alternating feature arrays;
- every variant validated against an independent edge-list FP64 oracle before timing;
- speedup below is relative to the single-thread portable B-ABI shim for the same case.

| Family | f | Four-thread variant | Median per call (ms) | Preliminary speedup | Partial bytes |
|---|---:|---|---:|---:|---:|
| uniform | 16 | row_static | 0.2733 | 2.84x | 0 |
| uniform | 16 | row_dynamic | 1.1740 | 0.66x | 0 |
| uniform | 16 | split_row | 0.2318 | 3.34x | 0 |
| uniform | 64 | row_static | 0.4985 | 3.80x | 0 |
| uniform | 64 | row_dynamic | 2.4186 | 0.78x | 0 |
| uniform | 64 | split_row | 0.7632 | 2.48x | 0 |
| skewed | 16 | row_static | 0.5322 | 1.48x | 0 |
| skewed | 16 | row_dynamic | 0.5874 | 1.34x | 0 |
| skewed | 16 | split_row | 0.2493 | 3.17x | 68,224 |
| skewed | 64 | row_static | 1.2836 | 1.52x | 0 |
| skewed | 64 | row_dynamic | 2.3806 | 0.82x | 0 |
| skewed | 64 | split_row | 0.8274 | 2.36x | 272,896 |

Interpretation is deliberately limited. The results show that the scheduling
implementation exposes the expected qualitative behaviour on the test machine:
static row partitioning loses balance on skewed degrees, while split-row recovers
parallel work at the cost of descriptor and partial storage. Dynamic scheduling
with one-row tasks is too expensive for these short calls. Actual crossover points
must be re-measured with B's NEON kernel and A's production generator/oracle on the
agreed AArch64 platform.

The machine-readable source is `preliminary_x64_contract.csv`. Its environment
column is `portable_x64_validation_only`, and all 28 rows have
`fp64_validation=true`.

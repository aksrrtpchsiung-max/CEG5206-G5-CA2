# Experiment and Results Agreement v0.1

## Algorithm lifecycle and timing

Generate canonical edges/CSR/X → validate → prepare (separately timed) → audit split
chunks → repeatedly validate both X arrays → release oracle edges/reference →
warm up → time complete run calls → save results.

Use C++ steady_clock. Observe all Y elements between calls without assuming full
cache residency. Main/single runs use matching seeds/configuration to construct
equivalent inputs. Separate variant processes avoid retaining several workspaces
at once.

Main timings exclude graph/feature generation, allocation, validation, file I/O,
and prepare. They include all necessary initialization, scheduling, synchronization,
and merging inside run. Report graph sampling, weight preparation, CSR construction,
and variant preparation separately. shared_prep = weight_preparation +
csr_construction; report sampling separately.

## Tables

CSV files use UTF-8 and snake_case, seconds for time, and bytes for allocation.
Missing coordinates/baselines are blank; non-finite relative error is inf.
Every table includes case configuration, case_id, and run_id. Within a batch,
run_id equals case_id; use the manifest to distinguish batches. case_id is the
first 16 hexadecimal digits of the normalized configuration's SHA-256, not an
input-content digest.

| Table | Main fields |
|---|---|
| Shared configuration | contract_version, generator_version, phase, variant, family, n, m, f, graph_seed, feature_seed_0/1, threads, tile_features, max_chunk_edges, warmup, repetitions, memory_budget_bytes |
| calls | repetition (zero-based), batch_calls (currently 1), feature_seed, call_seconds |
| validation | feature_seed, passed, checked, failed, atol, rtol, max_abs_error, max_rel_error, max_scaled_error, worst_row, worst_feature |
| summary | median_seconds, iqr_seconds, scalar_speedup, thread_speedup, nominal_gflops, checksum |
| setup_memory | graph_sampling_seconds, weight_preparation_seconds, csr_construction_seconds, shared_prep_seconds, variant_prep_seconds, graph_bytes, feature_bytes, output_bytes, descriptor_bytes, partial_bytes, other_workspace_bytes, validation_retained_bytes, performance_retained_bytes |
| degree | degree_min/max/mean/median/p90/p99, empty_rows |

scalar_speedup = matching single-thread scalar median / current median.
thread_speedup = matching single-thread median of the same variant / current median.
Both require matching family/n/m/f/seeds/tile/chunk; leave blank without a baseline.
nominal_gflops = 2*m*f/(1e9*t), excluding extra merge operations.
IQR = Q(.75)-Q(.25), with linear interpolation at sorted index (N-1)*q.

## Memory accounting and limits

The budget is at most 2^30 bytes. Logical lower bounds: CSR=8*(n+1)+8*m;
two X arrays plus Y=12*n*f; partials=4*f*C_s, counting only chunks of split rows.
Use actual vector capacity*sizeof(T) and implementation-reported workspace.
Logical sizes must not hide padding or reserved capacity. Check lower bounds,
post-generation capacity, post-prepare capacity, and reference capacity. Release
reference and oracle edges before performance timing.

**validation_retained_bytes is the simultaneously retained size of known arrays,
not an actual peak measurement.** The public interface cannot currently observe
generator/prepare temporary peaks or prevent an oversized allocation before prepare
returns. Therefore this value is not named validation_peak_bytes. Before formal
experiments, A/C must supply peak-memory evidence and D must record the method and
results before setting peak_memory_verified=true in hardware metadata. This flag
is a manual review declaration, not an automatic proof; record evidence in notes.
RSS may include runtime allocations and thread stacks and must be distinguished
from the project array budget. Allocated bytes are not DRAM traffic.

## Configurations and phases

- smoke: initial integration only, not final performance evidence. Missing
  algorithms or failed validation stop execution.
- main: 24 workloads × five variants, single-threaded, totaling 120 cases.
- scaling.example: assumes P=4. Update P and the thread list to the actual physical
  cores used.
- tuning: independent graph seeds, six parameter pairs × two graphs, min(4,P)
  threads. A later executor selects by the geometric mean of complete split-call times.
- reuse: plans R=1/10/100. A later executor measures shared and variant-specific
  preparation plus R complete calls, alternating two X arrays.

The last two phases only generate plans in v0.1 and cannot execute. Main/scaling
can execute after actual algorithms are linked, parameters are genuinely frozen,
and peak memory is reviewed. Before complete experiments, also validate fixed
fixtures, empty graphs, isolated nodes, explicit self-loops, signed weights, hubs,
chunk boundaries, feature tails, and races. Smoke success is not full project
correctness acceptance.

Failures write failure.json and mark the manifest failed. Completed cases are
available for diagnosis, not proof of a complete successful experiment. Output
directories must be new. Missing implementations create no performance directory;
formal result tables are never filled with simulated timings.

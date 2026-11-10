# Measurement and Results Definitions

## Lifecycle and statistics

Generate canonical edges, CSR and two X arrays; validate; measure 10 preparations;
audit split layout; validate X0/X1/X0/X1 with NaN-prefilled Y; release oracle edges
and FP64 reference; warm up; calibrate; record 15 batches; export results.

Calibration doubles call groups until a batch reaches 20 ms or 1024 groups. Smoke
uses 5 ms and diagnostics uses one unbatched call. An R-reuse group contains
exactly R complete calls. All variants alternate the two X arrays by the continuous
call index. Timers surround full groups only. The final batch output is observed
after timing. median_seconds describes per-call batch averages;
median_sequence_seconds describes per-R-call-group averages. These are warm-start
measurements. Consecutive samples are not independent cross-day replications.

Median and quartiles use linear interpolation at sorted index (N-1)*q.
IQR=Q(.75)-Q(.25). Primary timings include necessary clearing, aggregation,
scheduling, synchronization and merging; they exclude allocation, generation,
prepare, validation and file I/O. Variant prepare is a median of 10 fresh states;
state destruction is outside its timer. Shared setup is weight preparation plus
CSR construction, each recorded once per case. Sampling is separate.

T(R)=shared_prep+median_variant_prep+median_R_call_sequence. Feature generation,
graph sampling and validation are outside this reuse convention. The same graph
and state remain live during the sequence. Reuse does not remove per-call merges.

## Files and units

CSV files are UTF-8 with stable snake_case names. Time is seconds, capacity is
bytes, repetition starts at zero, missing baselines/coordinates are blank, and
non-finite relative error is inf. Tables are calls, validation, summary,
setup_memory and degree. Configuration accompanies each row. run_id equals the
configuration-derived case_id within its parent manifest. case_id is not an input
digest. Input digest is FNV-1a64 over native bytes of row_ptr, col_idx, values,
X0 and X1, intended for consistency checks on the same architecture.

Raw calls include batch_calls, batch_groups, reuse_calls, feature_seed at batch
start, call_seconds and sequence_seconds. X alternates throughout the batch;
a starting seed does not imply all calls used one seed. Summary speedups require
matching family/n/m/f/seeds/tile/chunk/R and the appropriate scalar or own-variant
one-thread baseline. Nominal throughput is 2*m*f/(1e9*t), excluding extra merges.

Per-case JSON retains 10 preparation samples, two final validation reports, input
digest, power source/charge, and diagnostic samples when enabled. Every validation
execution must pass before any timing output is accepted. The two stored reports
are the final X0/X1 checks after four validation executions.

## Memory evidence

The logical payload is CSR=8*(n+1)+8*m; two X arrays plus Y=12*n*f;
partials=4*f*C_s for chunks of split rows only. Retained arrays use actual vector
capacity and sizeof, including descriptor padding. Workspace fields do not overlap.
Validation retained bytes are not labeled as peak bytes.

An operator-new tracker measures the high-water sum of requested live C++
allocations over all phases, including unordered-set nodes, sorting temporaries,
worker bins and vector capacity. It excludes allocator bookkeeping and runtime
malloc/mmap allocations. getrusage process peak RSS is separately retained;
it includes the allocator/runtime/stacks and has a different interpretation.
The analyzer checks measured C++ peaks against the 1 GiB array budget. RSS is
reported rather than equated with array bytes or DRAM traffic.

## Experimental controls and interpretation

The campaign uses separate tuning seeds, freezes one selected pair and then runs
24 single-thread workloads, selected thread-scaling cases, R-reuse, independent
SIMD/tiling ablation and separate phase/worker diagnostics. Selection minimizes
the geometric mean of two full split-call medians at min(4,P) threads. Case order
is deterministically shuffled. Every phase records actual configuration and order.

No hard affinity or frequency control is claimed. The M4 contains four P and six
E cores. Equal edge counts need not mean equal elapsed work; dynamic scheduling
can properly assign more edges to faster cores. Instrumented phase medians are
supporting diagnostics and are not substituted for uninstrumented timings.

Failure preserves diagnostics and marks the manifest failed. Completed subsets
are partial evidence. Fresh output directories prevent mixing runs. Formal M4
results are not mixed with earlier M5, QEMU or portable-shim measurements.

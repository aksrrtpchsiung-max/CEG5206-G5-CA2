# Member D Native Experiments and English Report

D integrates the production A/B/C implementations, validates them on an Apple M4,
and measures complete aggregation calls. All D documentation, figures, and report
content are in English. Public algorithm declarations remain contract v0.1.

## Deliverables

- [Experimental report](report/CEG5206_Group5_Experimental_Report.pdf)
- [Editable report](report/report.md)
- [English figures in PNG and SVG](figures/)
- [Audited results and scaling table](report/audited_facts.json)
- [Native raw evidence](evidence/apple-m4/run-20261110/)
- [Native build and validation logs](evidence/apple-m4/)
- [Handoff and integration notes](docs/HANDOFF.md)
- [Measurement and memory definitions](docs/RESULTS.md)
- [Validation record](docs/VALIDATION.md)

This integrated branch includes A 43842aa, B 2c8f04f, C 8a06e1b, and D's public
entry e6921eb, followed by D's native experiment work. Earlier M5 and QEMU data
are retained in their original member directories but are not mixed into M4
performance results.

## Build and native validation

The production B kernel requires AArch64 NEON. The parallel C adapter requires
OpenMP. On Apple Silicon, install CMake and libomp if needed:

```bash
brew install cmake libomp
bash member_d/scripts/build_team.sh
python3 member_d/scripts/run.py list --binary member_d/build/team/benchmark
```

The script supplies Homebrew's keg-only OpenMP paths to Apple Clang and builds
`benchmark` plus `benchmark_profile`. Set `OMP_PREFIX` if libomp is elsewhere.
On native AArch64 Linux, use the same script with a compiler that finds OpenMP.

The production executable has no worker-counting or phase-timing hooks. The
separate diagnostic executable compiles the same C source with guarded hooks.
C++ allocation accounting is active in both executables and records high-water
requested bytes; its metadata is separate from process peak RSS.

A/B/C production kernels are called through shared `prepare/run` factories.
B's edge-major NEON control is additionally registered as `edge_major` for the
independent tiling ablation. It is not substituted for any production variant.

## Reproduce the campaign

The saved configuration targets this 10-core M4: four performance plus six
efficiency cores, 24 GiB memory, macOS 15.7.7, Apple Clang 17 and libomp 23.1.1.
Use a fresh output directory:

```bash
bash member_d/scripts/run_campaign.sh member_d/evidence/apple-m4/new-run
```

The script runs smoke and tuning, selects one pair using the geometric mean of
the two independent tuning graphs, freezes parameters, then runs main, scaling,
reuse, ablation and diagnostics. It updates `configs/apple-m4` with the newly
selected pair. Do not treat a future run's selected pair as fixed to this run's
outcome. Adjust physical cores, hardware metadata, power policy and paths before
running on another machine. Do not use an emulator for final performance claims.

Execution order is deterministically randomized. The main grid has 24 workloads
and five variants at one thread. Scaling uses 1, 2, 4, 8 and 10 requested threads.
Reuse uses R=1/10/100 on n=8192, mean degree=32, f=64, including one-thread controls
and one/four-thread parallel schedules. Power source and charge are saved per case.
No frequency lock or hard core affinity is claimed.

Every case validates both X inputs twice, warms up, calibrates batches and records
15 batch averages. Necessary output initialization, synchronization and merging
remain inside each timed complete call. Prepare is separately measured 10 times.
Raw JSON and CSV retain actual batch sizes and the R-call sequence measurements.

## Generate English figures and report

With Python 3.9 or later, install the report dependencies into a virtual environment
or other isolated environment:

```bash
python3 -m pip install -r member_d/requirements-report.txt
python3 member_d/scripts/analyze.py --campaign member_d/evidence/apple-m4/run-20261110 --figures member_d/figures --report member_d/report
python3 member_d/scripts/write_report.py --campaign member_d/evidence/apple-m4/run-20261110 --figures member_d/figures --report member_d/report
```

Analysis checks phase completion, validation results, input digests, positive
samples, worker edge coverage and allocation budgets before reporting facts.
Charts are generated from raw case records. The editable Markdown and PDF are
produced together from the same audited data. No simulated measurements are used.

## Entry point and output files

```bash
python3 member_d/scripts/run.py plan --config member_d/configs/apple-m4/main.json --out member_d/out/plan.json
python3 member_d/scripts/run.py run --config member_d/configs/apple-m4/smoke.json --binary member_d/build/team/benchmark --metadata member_d/configs/apple-m4/hardware.json --out member_d/out/new-smoke
```

Output directories must be new. Missing algorithms fail explicitly. A failed case
marks the manifest failed and preserves diagnostics; partial results are not a
completed experiment. Each phase includes configuration, source commit/patch,
binary SHA-256, runtime environment, execution order, raw calls, validation,
summary, setup/memory and degree tables. Each case JSON additionally includes
input digests, preparation samples, power observations and optional diagnostics.

## Scope

The report covers weighted CPU aggregation only. It does not claim full GNN
training speed, accuracy, GPU performance, hardware-counter attribution, hard
P/E affinity, or a new native M4 ThreadSanitizer result. See the report for
within-run variation, power records and limitations. The template configurations
outside `configs/apple-m4` remain examples and are not the frozen M4 campaign.

## Demonstrate correctness and a representative comparison

```bash
OMP_DYNAMIC=FALSE OMP_PROC_BIND=FALSE OMP_WAIT_POLICY=ACTIVE KMP_BLOCKTIME=200 python3 member_d/scripts/demo.py --out member_d/out/new-demo
```

The demo uses actual A/B/C implementations, validates against FP64 and prints a
small scalar/SIMD/whole-row/split comparison. It does not replace the complete
archived campaign. Results go to a new local output directory.

# Member D Contribution Record

## Initial delivery

- Defined the unified list/plan/run entry, shared factory registration and CSV schema.
- Added configuration templates and interface handoff/review documentation.
- Built and tested the framework without representing missing algorithms as results.
- Published the English v0.1 entry on codex/member-d-benchmark-entry-v0.1.

## Native integration and final experiments

- Integrated A 43842aa, B 2c8f04f and C 8a06e1b on a separate experiment branch.
- Configured native AppleClang, AArch64 NEON and OpenMP on Apple M4.
- Ran production release, ASan/UBSan, member tests and harness checks; preserved logs.
- Added calibrated batching, measured R-call reuse groups, repeated preparation
  timings, randomized case order, per-case power records and input/binary hashes.
- Added C++ allocation high-water accounting and independent process RSS evidence.
- Added guarded phase/worker hooks to C; production arithmetic and ownership remain
  unchanged. Diagnostic worker counters are padded to avoid added false sharing.
- Selected one Tf/Te pair using separate-seed tuning graphs and froze it before
  formal experiments. Executed main, scaling, reuse, ablation and diagnostics.
- Audited phase completion, input equality, validation, samples, memory budgets and
  diagnostic edge coverage. Generated English PNG/SVG figures and the English report.
- Added a reproducible native demonstration and report-generation dependencies.

Numerical totals and performance conclusions are generated from actual campaign
files rather than copied into code. Member attribution for A/B/C remains in their
original source and reports. D's native results do not include prior M5 or QEMU
measurements. No new M4 race-detector or hardware-counter result is claimed.

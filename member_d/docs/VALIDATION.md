# Native M4 Validation Record

Date: 10 November 2026. Platform: Apple M4, macOS 15.7.7, Apple Clang 17.0.0,
CMake 4.4.3 and libomp 23.1.1. All tests use production A/B/C where applicable.

- A+B+C Release: 7/7 CTest suites passed, including 1/2/4/10-thread variant tests.
- A+B+C ASan and UBSan: 7/7 suites passed, separate from performance binaries.
- B verification script: optimized, ASan/UBSan, concurrency, assembly and D
  fixture linkage checks passed. Fixtures are not used in production experiments.
- C native tests: 2/2 passed with the real B NEON implementation.
- D Python entry/export checks: 10/10 passed, no skips.
- D CTest: 2/2 passed for measurement/reuse and allocation accounting, including
  aligned allocations and release tracking.
- Production object disassembly confirms scalar fmul/fadd and four-lane NEON
  fmul.4s/fadd.4s; assembly evidence is retained.
- The campaign analyzer checks all phase manifests, sample counts, positive
  durations, FP64 validation records, input-digest equality, worker edge totals
  and C++ high-water memory budgets before creating report facts.

ASan/UBSan is not a race detector. A's provided cross-review contains separate
x86 TSan/Archer evidence for C; this report does not claim native M4 TSan coverage.
Diagnostic hooks are compiled out of production timings. The current campaign's
numerical totals and case counts are generated in report/audited_facts.json.

Logs and raw evidence are under evidence/apple-m4. Earlier v0.1 harness tests and
prior members' measurements remain historical evidence, not substitutes for these
native M4 results. Any preliminary campaign startup error in campaign.log is
retained; phase manifests and the final audit determine completion.

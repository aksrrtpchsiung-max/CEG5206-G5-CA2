# CEG5206 Group 5 CPU Graph Aggregation

This integrated branch contains the production A/B/C implementations and D's
native Apple M4 experimental pipeline, raw evidence, English figures and report.

- [Member A baseline generation and correctness](member_a/README.md)
- [Member B feature tiling and NEON](member_b/README.md)
- [Member C row scheduling and split reduction](member_c/README.md)
- [Member D native experiments and report](member_d/README.md)
- [English experimental report](member_d/report/CEG5206_Group5_Experimental_Report.pdf)

The computation is weighted CSR aggregation Y=AX on CPU, not full GNN training.
See Member D's documentation for reproducible builds, tests and measurement.

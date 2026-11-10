# Member A · Methods and results for the report

## Numerical reference

For each destination row, the baseline clears all output features and walks the stored edges in CSR order. Each edge contributes its stored FP32 weight times the source node's feature vector. Input and output features are contiguous by row. Empty rows are written as zero, signed weights are preserved and self-loops contribute only when explicitly stored.

The oracle starts from the original weighted edge list captured before CSR construction. It converts both operands to double before multiplication and scatters each contribution into the FP64 destination vector. The test suite changes a weight in the completed CSR and checks that comparison with the original oracle detects the corruption. A separate test uses a product whose last significant bit would be lost if multiplication occurred in float. The fixed handoff also compares the oracle with hand-calculated values. [Handoff fixture](../fixtures/contract_v0_1.json)

For finite values, an element passes when its absolute error is at most `1e-5 + 1e-4 * abs(reference)`. We report the largest absolute and relative errors and the largest ratio of absolute error to that allowance. A ratio of 1 is the acceptance boundary. This matters near a zero reference, where the relative error alone can be large. Nonfinite results fail validation.

## Deterministic inputs and setup

The generator uses a seeded MT19937-64 engine, rejection sampling for bounded integers and sampling without replacement. Each row has sorted, unique, directed neighbours. Uniform rows share the requested edges evenly. Skewed graphs assign half the edges, with integer rounding, to a randomly chosen group of ceil(n/100) nodes. Each benchmark edge stores the rounded FP32 reciprocal of its row degree. The same n and m are used for both families. Two feature seeds generate reproducible inputs in [-1, 1). Small fixed-seed golden tests pin both neighbour IDs and feature values.

Sampling, FP32 weight preparation and CSR construction have independent steady-clock timers. The scalar run allocates no workspace. Validation and oracle calls run outside the timed aggregation. The optimized GCC build disables automatic vectorization only for the scalar translation unit and disables fast math and contraction across numerical sources. In the AArch64 disassembly, the inner loop contains standalone `fmul s30, s31, s30` and `fadd s29, s29, s30`. On Windows x64 the MSVC Release build produced scalar `mulss` and `addss` for accumulation.

## Correctness on the proposal grid

On November 6, 2026, the x86_64 GNU Release build checked 24 workloads at two feature seeds each. The sweep compared **198,180,864 scalar outputs**, with **zero failures**. The maximum absolute error was **1.52737 × 10⁻⁷**. The largest fraction of the allowed error was **0.00787974**. It occurred on the skewed graph with n=65,536, m=2,097,152, f=256 and feature seed 5206101 at row 60,257, feature 219. That row had degree 1,598. The data support retaining the proposed tolerance on this grid. [All 48 validation records](../evidence/scalar-grid.csv)

For the separate skewed case n=8,192, m=262,144, f=17, maximum degree was 1,599. On X0, the largest scaled error was 0.00516696 for scalar and 0.00448502 for split-row at four threads. Splitting changes the addition order by summing partial vectors first. In both cases all 139,264 elements passed. The largest relative error on X0 was about 0.0204, while the largest absolute error was 8.94 × 10⁻⁸. These are separate maxima and can occur at different coordinates. [Hub results](../evidence/hub-arm.csv)

## Memory and scope of evidence

The grid sweep retained both X arrays, Y, one FP64 reference, the original edge list and the CSR. Using actual vector capacities, the maximum was 378,011,656 bytes (360.50 MiB). The Linux process high-water RSS was 392,480 KiB (383.28 MiB). RSS also reflects the allocator, runtime and test process, while the capacity count measures the named arrays.

Generation retains canonical edges and a degree array of n offsets. During sampling, the largest temporary neighbour set and sorted vector scale with a single row's degree. During CSR construction, the sorted canonical edges are reused directly. The public builder for arbitrary unsorted inputs may additionally sort a copied edge list, and duplicate checking may temporarily allocate one 64-bit key per edge. The benchmark generator directly constructs sorted unique input and avoids both unsorted-input copies. Degree statistics use a temporary n-element offset array. C's prepare-time temporary memory must be accounted for separately by C/D.

The D smoke program exercised the five implementations with two graph families and both X inputs. The 48 cases produced 96 passing validation records (2,719,744 checked outputs) and 480 individual timed-call records. The largest scaled error was 0.00390372. D's largest reported retained validation allocation was 1,631,336 bytes. ARM instructions ran under QEMU on an Intel host, so these records establish functional integration. Formal speedup, scaling and profiling require the team's physical target CPU and D's experimental protocol. [D smoke validation](../evidence/team-smoke.csv)

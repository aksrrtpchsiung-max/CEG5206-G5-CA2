# CEG5206-G5-CA2

- [Member A: public interface proposal v0.1](member_a/README.md)
- [Member B: feature tiling, NEON SIMD, safe tails, and validation](member_b/README.md)
- [Member D: unified benchmark entry and handoff v0.1](member_d/README.md)

The `binzhi` branch adds B's complete single-thread SIMD operator and reusable
row-range kernel on top of A's interface proposal and D's integration framework.
A's public header is unchanged. B includes standalone tests and explicitly labelled
local attribution measurements; the complete team's A/C implementations and formal
experiment results are not included. See B's handoff for integration commands.

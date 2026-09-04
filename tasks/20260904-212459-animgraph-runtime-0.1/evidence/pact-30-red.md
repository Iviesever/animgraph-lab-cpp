# PACT-30 RED evidence

- Command: `scripts/run_mqb_tests.ps1 -Configuration Debug`
- Exit code: 1
- Failure: missing `animgraph/compression/compression.hpp`.
- Interpretation: all six procedural compression contracts were compiled before
  the production compression API existed.

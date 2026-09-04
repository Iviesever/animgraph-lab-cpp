# PACT-50 RED evidence

- Command: `scripts/run_mqb_tests.ps1 -Configuration Debug`
- Exit code: 1
- Failure: missing `animgraph/runtime/blend.hpp`.
- Interpretation: Blend1D/2D, additive, layer hierarchy, state transition, marker,
  root seam, and compiled-node tests existed before the PACT-50 interfaces.

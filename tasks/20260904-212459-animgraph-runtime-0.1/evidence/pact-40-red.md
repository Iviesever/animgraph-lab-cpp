# PACT-40 RED evidence

- Command: `scripts/run_mqb_tests.ps1 -Configuration Debug`
- Exit code: 1
- Failure: missing `animgraph/graph/graph.hpp`.
- Interpretation: graph validation, topology, liveness, identity, evaluator, cache,
  and clip-player tests existed before their production interfaces.

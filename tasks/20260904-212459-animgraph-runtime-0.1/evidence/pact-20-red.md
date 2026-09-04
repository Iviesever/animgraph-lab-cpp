# PACT-20 RED evidence

- Command: `scripts/run_mqb_tests.ps1 -Configuration Debug`
- Exit code: 1
- Failure: `fatal error C1083: cannot open include file: animgraph/asset/codec.hpp`
- Interpretation: the clip/time/event/marker/asset contract suite reached the
  absent production interface before PACT-20 implementation.

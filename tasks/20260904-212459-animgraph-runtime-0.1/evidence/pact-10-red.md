# PACT-10 RED evidence

- Command: `scripts/run_mqb_tests.ps1 -Configuration Debug`
- Exit code: 1
- Failure: `fatal error C1083: cannot open include file: animgraph/math/math.hpp`
- Interpretation: the new math/skeleton contract suite reached the deliberately
  absent production interface before implementation.

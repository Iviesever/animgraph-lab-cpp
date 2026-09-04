# PACT-00 RED evidence

- Time: 2026-09-04 21:31 CST
- Command: `mqb build tests/test_main.cpp tests/version_tests.cpp --std 23 --debug -j 4 -I include -I tests /EHsc /W4 /WX /permissive- /utf-8 -o animgraph_tests --timings=json`
- Exit code: 1
- Intended failure: `fatal error C1083: cannot open include file: animgraph/core/version.hpp`
- Interpretation: MQB discovered and invoked MSVC; the test translation unit failed
  because the version production interface did not yet exist.

The earlier CMake configure attempt also exited 1 because this machine has no
registered Visual Studio instance. That was an environment failure rather than the
semantic RED and is retained here as a separate portability boundary observation.
